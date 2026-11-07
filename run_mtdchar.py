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

from boundary_common import HERE, bodies, check
import run as tlc

REFERENCE = HERE / "reference/mtdchar-chunks"
FUNCTIONS = ["adjust_oob_length", "mtdchar_adjust_io_lengths",
             "mtdchar_write_ioctl", "mtdchar_read_ioctl"]
MODEL = dict(invariants=["SourcePlan", "SourceBytes", "SourceBounds", "SourceOnce",
                         "SourceReturn"], actions=["Next"],
             dependencies=["NandMtdCharChunks"])


def snapshot():
    pin = json.loads((REFERENCE / "source.json").read_text())
    result = {}
    for name, expected in pin["files"].items():
        data = (REFERENCE / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("mtdchar reference hash mismatch: " + name)
        result[name] = data.decode()
    return result, pin


def source_checks(source, core, abi, *, baseline=False, cc="gcc"):
    names = [n for n in FUNCTIONS if n != "mtdchar_adjust_io_lengths" or not baseline]
    src = {"mtdchar.c": source, "mtdcore.c": core}
    funcs = {"mtdchar.c": names, "mtdcore.c": ["mtd_check_oob_ops"]}
    kernel, _, hashes = bodies(src, funcs)
    env = (HERE / "tests/mtdchar_env.h").read_text()
    tests = (HERE / "tests/mtdchar_chunks.c").read_text()
    unit = env + kernel + '\n#line 1 "mtdchar-chunks.c"\n' + tests
    out = check("mtdchar-baseline" if baseline else "mtdchar-candidate",
                {"check.c": unit, "mtd-abi.h": abi}, hashes, cc=cc,
                args=["baseline" if baseline else "fixed", "calls.jsonl"],
                flags=["-DCHECK_FIXED=" + str(int(not baseline))],
                expected="MTDCHAR CHUNK CHECKS PASSED", artifacts=["calls.jsonl"],
                coverage=(src, funcs),
                limits=["Extracted ioctl/helper bodies; allocator and usercopy are hosted shims",
                        "Page backends model raw-NAND data-driven and SPI-NAND symmetric iteration",
                        "Raw-NAND multi-page OOB-only writes are rejected, not silently emulated",
                        "Subpage writes use a permissive backend; device-specific granularity is excluded",
                        "No physical NAND, encoding, DMA, unbind, concurrent writes or hardware coverage"])
    return out, [json.loads(line) for line in (out / "calls.jsonl").read_text().splitlines()]


def immutable(row):
    return dict(row, calls=tuple(tuple(c) for c in row["calls"]),
                reported=tuple(row["reported"]))


def trace_cases(candidate, baseline):
    cases = []

    def add(name, rows, *, expected=None, plan=True, kind="positive"):
        cases.append(dict(module="NandMtdCharTrace", name="mtdchar-source-"+name,
                          expected=expected, kind=kind,
                          constants=dict(Trials=tuple(map(immutable, rows)),
                                         CheckPlan=plan, Mutation="none")))

    # Separate configurations avoid a gigantic TLC wrapper and retain a clear
    # geometry/backend coverage record. Every generated C case is checked.
    for width in sorted({r["width"] for r in candidate}):
        add("w"+str(width), [r for r in candidate if r["width"] == width])
    old_oob = [r for r in baseline if not r["hasData"] and r["read"]
               and len(r["calls"]) > 1]
    add("baseline-oob-repeat", old_oob, expected="SourceBytes", plan=False,
        kind="baseline")
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
    parser.add_argument("--candidate-source", type=Path,
                        help="full current mtdchar.c instead of the pinned local v1")
    parser.add_argument("--source-only", action="store_true")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    src, pin = snapshot()
    candidate = args.candidate_source.read_text() if args.candidate_source else src["candidate.c"]
    old_out, old = source_checks(src["baseline.c"], src["mtdcore.c"], src["mtd-abi.h"],
                                 baseline=True, cc=args.cc)
    new_out, new = source_checks(candidate, src["mtdcore.c"], src["mtd-abi.h"], cc=args.cc)
    if args.source_only:
        return 0
    opts = ["--timeout", str(args.timeout)] + (["--download"] if args.download else [])
    return tlc.main(opts, cases=trace_cases(new, old), modules={"NandMtdCharTrace": MODEL},
                    metadata=dict(source_pin=pin, candidate_source_sha256=hashlib.sha256(
                        candidate.encode()).hexdigest(),
                        source_reports=[str(p.relative_to(HERE)) for p in (old_out, new_out)]))


if __name__ == "__main__":
    raise SystemExit(main())
