#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute pinned probe/scan/cleanup and ONFI negotiation bodies with fault injection."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from run_core import extract
from run_c import oob_policy_defines, oob_policy_functions, oob_policy_tables
from source_coverage import build_and_collect

HERE = Path(__file__).resolve().parent
FUNCTIONS = {
    "sunxi_nand.c": ["to_sunxi_nand", "to_sunxi_nfc", "sunxi_nand_detach_chip",
                     "sunxi_nand_attach_chip", "sunxi_nand_chips_cleanup",
                     "sunxi_nand_chip_init", "sunxi_nand_chips_init",
                     "sunxi_nfc_probe", "sunxi_nfc_remove"],
    "nand_base.c": ["nand_supports_get_features", "nand_supports_set_features",
                    "nand_get_device", "nand_release_device",
                    "nand_reset_interface", "nand_setup_interface", "nand_scan_ident",
                    "nand_scan_ident_cleanup", "nand_scan_tail", "nand_attach",
                    "nand_detach", "nand_scan_with_ids", "nand_cleanup"],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--candidate-source", type=Path,
                        help="test this sunxi_nand.c without replacing the pinned reference")
    parser.add_argument("--candidate-nand-base", type=Path,
                        help="nand_base.c containing the timing recovery error fix")
    parser.add_argument("--expect-available-children", action="store_true",
                        help="expect candidate to skip unavailable NAND children")
    parser.add_argument("--expect-deferred-publication", action="store_true",
                        help="expect candidate to scan all chips before MTD registration")
    parser.add_argument("--probe-traces", action="store_true",
                        help="record extra source-executed probe/unwind traces for TLC")
    args = parser.parse_args()
    if args.expect_available_children and not args.candidate_source:
        parser.error("--expect-available-children requires --candidate-source")
    if args.expect_deferred_publication and not args.candidate_source:
        parser.error("--expect-deferred-publication requires --candidate-source")
    pin = json.loads((HERE / "source.json").read_text())
    sources = {name: (HERE / "reference" / name).read_text() for name in FUNCTIONS}
    bodies, prototypes, hashes = [], [], {}
    for name, names in FUNCTIONS.items():
        src = sources[name]
        expected = pin["driver_sha256"] if name == "sunxi_nand.c" else pin["core_sources"][name]["sha256"]
        if hashlib.sha256(src.encode()).hexdigest() != expected:
            parser.error("source hash mismatch: " + name)
        if name == "nand_base.c" and args.candidate_nand_base:
            src = args.candidate_nand_base.read_text()
            sources[name] = src
        if name == "sunxi_nand.c" and args.candidate_source:
            src = args.candidate_source.read_text()
            sources[name] = src
        if name == "sunxi_nand.c":
            names.extend(oob_policy_functions(src))
        for func in names:
            body = extract(src, func)
            prototypes.append(body[:body.index("\n{")] + ";")
            line = src[:src.index(body)].count("\n") + 1
            bodies.append(f'#line {line} "{name}"\n{body}')
            hashes[func] = hashlib.sha256(body.encode()).hexdigest()
    results = HERE / "results"
    results.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="lifecycle-", dir=results))
    if "bool use_randomizer;" in sources["sunxi_nand.c"]:
        assignments = re.findall(r"\boob_ops\s*=(?!=)", sources["sunxi_nand.c"])
        assert len(assignments) == 1, "runtime OOB policy must be installed at one source site"
    fixture = (HERE / "tests/lifecycle.h").read_text()
    fixture = fixture.replace("/* @oob-policy-tables@ */", oob_policy_tables(sources["sunxi_nand.c"]))
    fixture = fixture.replace("/* @prototypes@ */", "\n".join(prototypes))
    tests = (HERE / "tests/lifecycle.c").read_text()
    if args.probe_traces:
        tests = ('#define main lifecycle_main\n' + tests + '\n#undef main\n' +
                 (HERE / "tests/probe_trace.c").read_text())
    (out / "check.c").write_text(f'#define FIXED_TIMING {int(bool(args.candidate_nand_base))}\n' +
        f'#define MANDATORY_RESET {int("devm_reset_control_get_exclusive(dev" in sources["sunxi_nand.c"])}\n' +
        f'#define PULSE_RESET {int("reset_control_reset(nfc->reset)" in sources["sunxi_nand.c"])}\n' +
        f'#define FILTERED_CHILDREN {int(args.expect_available_children)}\n' +
        f'#define DEFERRED_PUBLICATION {int(args.expect_deferred_publication)}\n' +
        f'#define DEFERRED_SELECTION {int("sunxi_nfc_select_oob_ops(" in sources["sunxi_nand.c"])}\n' +
        f'#define HAS_VENDOR {int("sunxi_nfc_allwinner_oob_attach_chip(" in sources["sunxi_nand.c"])}\n' +
        oob_policy_defines(sources["sunxi_nand.c"]) + fixture + "\n" + "\n".join(bodies) +
        '\n#line 1 "lifecycle-tests.c"\n' + tests)
    command = [args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
               "-fno-strict-aliasing", "-fno-strict-overflow", "-funsigned-char",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               "-fno-omit-frame-pointer", "-no-pie", "check.c", "-o", "check"]
    build = subprocess.run(command, cwd=out, text=True, capture_output=True)
    (out / "build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        print(build.stdout + build.stderr)
        print("Logs:", out)
        return 1
    run = subprocess.run(["./check"], cwd=out, text=True, capture_output=True, timeout=180)
    (out / "check.log").write_text(run.stdout + run.stderr)
    passed = run.returncode == 0 and "ALL LIFECYCLE CHECKS PASSED" in run.stdout and not run.stderr
    if args.probe_traces:
        passed = passed and "ALL PROBE TRACE CHECKS PASSED" in run.stdout
    trace_bytes = (out / "probe-traces.jsonl").read_bytes() if passed and args.probe_traces else None
    coverage = build_and_collect(out, command, sources, FUNCTIONS) if passed else None
    if trace_bytes is not None:
        assert trace_bytes == (out / "probe-traces.jsonl").read_bytes(), "sanitizer/GCOV traces differ"
    if coverage:
        assert all(v["calls"] for v in coverage["functions"].values()), "unexecuted lifecycle function"
    (out / "summary.json").write_text(json.dumps(dict(passed=passed,
        driver_sha256=hashlib.sha256(sources["sunxi_nand.c"].encode()).hexdigest(), functions=hashes,
        fixture_sha256=hashlib.sha256((out / "check.c").read_bytes()).hexdigest(),
        candidate=bool(args.candidate_nand_base or args.candidate_source),
        filtered_children=args.expect_available_children,
        deferred_publication=args.expect_deferred_publication,
        has_vendor="sunxi_nfc_allwinner_oob_attach_chip(" in sources["sunxi_nand.c"],
        probe_traces_sha256=hashlib.sha256(trace_bytes).hexdigest() if trace_bytes is not None else None,
        probe_trace_count=len(trace_bytes.splitlines()) if trace_bytes is not None else 0,
        source_sha256={n: hashlib.sha256(s.encode()).hexdigest() for n, s in sources.items()},
        output=run.stdout, diagnostics=run.stderr, coverage=coverage), indent=2) + "\n")
    print(run.stdout + run.stderr)
    print("Logs:", out)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
