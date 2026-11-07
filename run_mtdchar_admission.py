#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reproduce the separate pre-existing MEMREAD uninitialized-statistics path."""
import argparse
import hashlib
from pathlib import Path

from boundary_common import HERE, bodies, check
from run_mtdchar import TLC, snapshot
import run as tlc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-only", action="store_true")
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--candidate-source", type=Path,
                        help="also check an actual mtdchar.c with the statistics fix")
    args = parser.parse_args()
    source, pin = snapshot()
    wrapper = source["read-wrapper.c"]
    outputs = []
    variants = [("baseline", False, source["baseline.c"]),
                ("candidate", False, source["candidate.c"]),
                ("candidate", True, source["candidate.c"])]
    if args.candidate_source:
        variants.append(("submitted", True, args.candidate_source.read_text()))
    for name, initialized, mtdchar in variants:
        src = {"mtdchar.c": mtdchar,
               "mtdcore.c": source["mtdcore.c"] + wrapper}
        if name == "candidate" and initialized:
            before = "struct mtd_req_stats stats;"
            assert src["mtdchar.c"].count(before) == 1
            src["mtdchar.c"] = src["mtdchar.c"].replace(before, "struct mtd_req_stats stats = {};")
        functions = {"mtdcore.c": ["mtd_check_oob_ops", "mtd_read_oob"],
                     "mtdchar.c": ["adjust_oob_length"] +
                        (["mtdchar_adjust_io_lengths"] if "mtdchar_adjust_io_lengths(" in mtdchar else []) +
                        ["mtdchar_read_ioctl"]}
        kernel, _, hashes = bodies(src, functions)
        unit = (HERE / "tests/mtdchar_env.h").read_text()
        unit += (HERE / "tests/mtdchar_admission.h").read_text() + kernel
        unit += '\n#line 1 "admission.c"\n' + (HERE / "tests/mtdchar_admission.c").read_text()
        out = check("mtdchar-admission-"+name+str(int(initialized)),
                    {"check.c": unit, "mtd-abi.h": source["mtd-abi.h"]}, hashes,
                    flags=["-DINIT_STATS="+str(int(initialized)), "-ftrivial-auto-var-init=pattern"],
                    expected="MTDCHAR ADMISSION CHECKS PASSED",
                    limits=["Actual MTD admission wrapper and MEMREAD bodies",
                            "Compiler pattern initialization makes uninitialized reads deterministic",
                            ("Submitted source is extracted without modifying its initializer"
                             if name == "submitted" else
                             "Pinned candidate initialization is a fixture-only experiment"),
                            "The test detects propagation of undefined stats, not a real stack disclosure exploit"])
        outputs.append(str(out.relative_to(HERE)))
    if args.source_only:
        return 0
    cases = [dict(module="NandMtdCharStats", name="mtdchar-stats-"+name,
                  kind="baseline" if expected else "positive", expected=expected,
                  constants=dict(Initialize=init, Reject=reject, Rounds=rounds))
             for name, init, reject, rounds, expected in (
                 ("rejected-uninitialized", False, True, 1, "DefinedStatistics"),
                 ("admitted-uninitialized", False, False, 1, None),
                 ("rejected-initialized", True, True, 1, None),
                 ("rejected-after-prefix", False, True, 2, "DefinedStatistics"),
                 ("rejected-prefix-initialized", True, True, 2, None))]
    return tlc.main(["--download"] if args.download else [], cases=cases, toolchain=TLC,
                    modules={"NandMtdCharStats": dict(invariants=["DefinedStatistics"],
                                                     actions=["Core", "Accumulate"])},
                    metadata=dict(source_pin=pin, wrapper_sha256=hashlib.sha256(wrapper.encode()).hexdigest(),
                                  source_reports=outputs))


if __name__ == "__main__":
    raise SystemExit(main())
