#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run pinned/candidate ioctl C, then check its call traces with TLC.

No hardware access. The two backend iterators are contract fixtures, not NAND
driver emulation. run_mtdchar_model.py additionally explores all fault positions.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from boundary_common import HERE, bodies, check
from mtdchar_cases import TLC
import run as tlc

REFERENCE = HERE / "reference/mtdchar-chunks"
FUNCTIONS = ["adjust_oob_length", "mtdchar_adjust_io_lengths", "mtdchar_next_offset",
             "mtdchar_write_ioctl", "mtdchar_read_ioctl"]
MODEL = dict(invariants=["SourcePlan", "SourceBytes", "SourceBounds", "SourceOnce",
                         "SourceReturn", "SourceComplete"], actions=["Next"],
             dependencies=["NandMtdCharChunks"])
TRACE_BATCH_STEPS = 8192


def snapshot():
    pin = json.loads((REFERENCE / "source.json").read_text())
    result = {}
    for name, expected in pin["files"].items():
        data = (REFERENCE / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("mtdchar reference hash mismatch: " + name)
        result[name] = data.decode()
    return result, pin


def v2_candidate(v1):
    """Apply the pinned review correction without altering the v1 control."""
    patch = HERE / "reference/mtdchar-chunks-v2/0001-mtdchar-finish-OOB-transfers-after-the-data-runs-out.patch"
    expected = "893b4b8569055fea4293077e7e129809bc5e04467144d788d390db8a004fab8f"
    if hashlib.sha256(patch.read_bytes()).hexdigest() != expected:
        raise ValueError("mtdchar v2 correction hash mismatch")
    with tempfile.TemporaryDirectory(prefix="mtdchar-v2-") as tmp:
        source = Path(tmp) / "drivers/mtd/mtdchar.c"
        source.parent.mkdir(parents=True)
        # The v1 pin contains only functions, with line directives between
        # them; restore the original blank separators for patch context.
        source.write_text(re.sub(r"^#line[^\n]*", "", v1, flags=re.MULTILINE))
        subprocess.run(["git", "apply", str(patch)], cwd=tmp, check=True,
                       capture_output=True, text=True)
        return source.read_text()


def source_checks(source, core, abi, *, baseline=False, tail_control=False, cc="gcc"):
    names = [n for n in FUNCTIONS if n + "(" in source]
    src = {"mtdchar.c": source, "mtdcore.c": core}
    funcs = {"mtdchar.c": names, "mtdcore.c": ["mtd_check_oob_ops"]}
    kernel, _, hashes = bodies(src, funcs)
    env = (HERE / "tests/mtdchar_env.h").read_text()
    tests = (HERE / "tests/mtdchar_chunks.c").read_text()
    unit = env + kernel + '\n#line 1 "mtdchar-chunks.c"\n' + tests
    out = check("mtdchar-v1-tail-control" if tail_control else
                "mtdchar-baseline" if baseline else "mtdchar-candidate",
                {"check.c": unit, "mtd-abi.h": abi}, hashes, cc=cc,
                args=["v1-tail-control" if tail_control else
                      "baseline" if baseline else "fixed", "calls.jsonl"],
                flags=["-DCHECK_FIXED=" + str(int(not baseline)),
                       "-DCHECK_TAIL_CONTROL=" + str(int(tail_control)),
                       "-DCHECK_OOB_TAILS=" + str(int("mtdchar_next_offset(" in source))],
                expected="MTDCHAR V1 TAIL CONTROL CHECKS PASSED" if tail_control else
                         "MTDCHAR CHUNK CHECKS PASSED", artifacts=["calls.jsonl"],
                coverage=(src, funcs),
                require_all_called=not tail_control,
                limits=["Extracted ioctl/helper bodies; allocator and usercopy are hosted shims",
                        "Page backends model raw-NAND data-driven and SPI-NAND symmetric iteration",
                        "Raw-NAND multi-page OOB-only writes are rejected, not silently emulated",
                        "Subpage writes use a permissive backend; device-specific granularity is excluded",
                        "No physical NAND, encoding, DMA, unbind, concurrent writes or hardware coverage"])
    return out, [json.loads(line) for line in (out / "calls.jsonl").read_text().splitlines()]


def immutable(row):
    return dict(row, calls=tuple(tuple(c) for c in row["calls"]),
                reported=tuple(row["reported"]))


def trace_batches(rows, max_steps=TRACE_BATCH_STEPS):
    """Bound TLC inputs without splitting or dropping an execution trace."""
    if max_steps <= 0:
        raise ValueError("trace batch budget must be positive")
    batch, steps = [], 0
    for row in rows:
        # Include initialization and Finish as well as the recorded calls.
        cost = len(row["calls"]) + 2
        if batch and steps + cost > max_steps:
            yield batch
            batch, steps = [], 0
        batch.append(row)
        steps += cost
    if batch:
        yield batch


def trace_cases(candidate, baseline, complete_oob=False):
    cases = []

    def add(name, rows, *, expected=None, plan=True, kind="positive", complete=None):
        cases.append(dict(module="NandMtdCharTrace", name="mtdchar-source-"+name,
                          expected=expected, kind=kind,
                          constants=dict(Trials=tuple(map(immutable, rows)),
                                         CheckPlan=plan, Mutation="none",
                                         CompleteOob=complete_oob if complete is None else complete)))

    # OOB-only tails can contain thousands of calls per trace. Bound each
    # wrapper by its steps, not just geometry, to fit TLC's 1-GiB heap.
    # Every complete trace is still checked with the same invariants.
    for width in sorted({r["width"] for r in candidate}):
        rows = (r for r in candidate if r["width"] == width)
        for index, batch in enumerate(trace_batches(rows)):
            add(f"w{width}-{index:03d}", batch)
    old_oob = [r for r in baseline if not r["hasData"] and r["read"]
               and len(r["calls"]) > 1]
    add("baseline-oob-repeat", old_oob, expected="SourceBytes", plan=False,
        kind="baseline", complete=False)
    row = next(r for r in candidate if r["data"] and r["oob"] and len(r["calls"]) > 1)
    altered = copy.deepcopy(row)
    altered["calls"][0][1] += 1
    add("tampered-plan", [altered], expected="SourcePlan", kind="mutation")
    altered = copy.deepcopy(row)
    altered["calls"][1][0] -= row["width"]
    add("tampered-address", [altered], expected="SourceBytes", kind="mutation")
    row = next(r for r in candidate if r["read"] and r["data"])
    altered = copy.deepcopy(row)
    altered["reported"][0] -= 1
    add("tampered-count", [altered], expected="SourceReturn", kind="mutation")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    candidate_options = parser.add_mutually_exclusive_group()
    candidate_options.add_argument("--candidate-source", type=Path,
                        help="full current mtdchar.c instead of the pinned local v1")
    candidate_options.add_argument("--v2", action="store_true",
                                   help="check the pinned v2 OOB-tail correction")
    parser.add_argument("--source-only", action="store_true")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    src, pin = snapshot()
    candidate = args.candidate_source.read_text() if args.candidate_source else src["candidate.c"]
    if args.v2:
        candidate = v2_candidate(candidate)
    source_checks(src["candidate.c"], src["mtdcore.c"], src["mtd-abi.h"],
                  tail_control=True, cc=args.cc)
    old_out, old = source_checks(src["baseline.c"], src["mtdcore.c"], src["mtd-abi.h"],
                                 baseline=True, cc=args.cc)
    new_out, new = source_checks(candidate, src["mtdcore.c"], src["mtd-abi.h"], cc=args.cc)
    if args.source_only:
        return 0
    opts = ["--timeout", str(args.timeout)] + (["--download"] if args.download else [])
    return tlc.main(opts, cases=trace_cases(new, old, "mtdchar_next_offset(" in candidate),
                    modules={"NandMtdCharTrace": MODEL},
                    toolchain=TLC,
                    metadata=dict(source_pin=pin, candidate_source_sha256=hashlib.sha256(
                        candidate.encode()).hexdigest(),
                        source_reports=[str(p.relative_to(HERE)) for p in (old_out, new_out)]))


if __name__ == "__main__":
    raise SystemExit(main())
