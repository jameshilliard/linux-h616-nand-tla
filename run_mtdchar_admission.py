#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reproduce the separate pre-existing MEMREAD uninitialized-statistics path."""
import argparse
import hashlib

from boundary_common import HERE, bodies, check
from run_mtdchar import TLC, snapshot
import run as tlc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-only", action="store_true")
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    source, pin = snapshot()
    wrapper = source["read-wrapper.c"]
    outputs = []
    for name, initialized in (("baseline", False), ("candidate", False), ("candidate", True)):
        src = {"mtdchar.c": source[name+".c"],
               "mtdcore.c": source["mtdcore.c"] + wrapper}
        if initialized:
            before = "struct mtd_req_stats stats;"
            assert src["mtdchar.c"].count(before) == 1
            src["mtdchar.c"] = src["mtdchar.c"].replace(before, "struct mtd_req_stats stats = {};")
        functions = {"mtdcore.c": ["mtd_check_oob_ops", "mtd_read_oob"],
                     "mtdchar.c": ["adjust_oob_length"] +
                        (["mtdchar_adjust_io_lengths"] if name == "candidate" else []) +
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
                            "Candidate initialization is a fixture-only experiment, not a kernel patch",
                            "The test detects propagation of undefined stats, not a real stack disclosure exploit"])
        outputs.append(str(out.relative_to(HERE)))
    if args.source_only:
        return 0
    cases = [dict(module="NandMtdCharStats", name="mtdchar-stats-"+name,
                  kind="baseline" if expected else "positive", expected=expected,
                  constants=dict(Initialize=init, Reject=reject))
             for name, init, reject, expected in (
                 ("rejected-uninitialized", False, True, "DefinedStatistics"),
                 ("admitted-uninitialized", False, False, None),
                 ("rejected-initialized", True, True, None))]
    return tlc.main(["--download"] if args.download else [], cases=cases, toolchain=TLC,
                    modules={"NandMtdCharStats": dict(invariants=["DefinedStatistics"],
                                                     actions=["Core", "Accumulate"])},
                    metadata=dict(source_pin=pin, wrapper_sha256=hashlib.sha256(wrapper.encode()).hexdigest(),
                                  source_reports=outputs))


if __name__ == "__main__":
    raise SystemExit(main())
