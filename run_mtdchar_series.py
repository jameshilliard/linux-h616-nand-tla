#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check each stage of the three-patch mtdchar split without accessing hardware.

The default reconstructs the two preparatory sources from pinned snapshots.
--sources checks actual full source files from the three commits instead.
Remaining baseline failures are permitted before the final patch, but a
previously passing request must never regress at an intermediate stage.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

from boundary_common import HERE
from run_core import extract
from run_mtdchar import FUNCTIONS, snapshot, source_checks


def stages(src):
    old, final = src["baseline.c"], src["candidate.c"]
    first = old.replace(extract(old, "adjust_oob_length"),
                        extract(final, "adjust_oob_length"), 1)
    call = "\t\tret = mtd_read_oob(mtd, (loff_t)req.start, &ops);"
    if first.count(call) != 1:
        raise ValueError("MEMREAD call-site correspondence changed")
    second = first.replace(call, "\t\tif (ops.len < req.len)\n"
                           "\t\t\tadjust_oob_length(mtd, req.start, &ops);\n\n" + call)
    return [first, second, final]


def compare(previous, current):
    if len(previous) != len(current):
        raise ValueError("request domains differ between stages")
    inputs = ("width", "spare", "avail", "erase", "size", "read",
              "symmetric", "hasData", "start", "data", "oob")
    recovered = 0
    for index, (before, after) in enumerate(zip(previous, current)):
        if any(before[key] != after[key] for key in inputs):
            raise ValueError(f"request {index} differs between stages")
        if before["good"] and not after["good"]:
            raise ValueError(f"previously passing request {index} regressed: {after}")
        recovered += not before["good"] and after["good"]
    return recovered


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, nargs=3,
                        metavar=("PATCH1", "PATCH2", "PATCH3"))
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    src, pin = snapshot()
    expected = stages(src)
    actual = [path.read_text() for path in args.sources] if args.sources else expected
    for number, (source, reference) in enumerate(zip(actual, expected), 1):
        names = [name for name in FUNCTIONS
                 if name + "(" in reference]
        for name in names:
            if extract(source, name) != extract(reference, name):
                raise ValueError(f"patch {number}: {name} does not match the pinned split")

    reports, trials, summary = [], [], []
    for number, source in enumerate([src["baseline.c"], *actual]):
        report, rows = source_checks(source, src["mtdcore.c"], src["mtd-abi.h"],
                                    baseline=number < 3, cc=args.cc)
        recovered = compare(trials[-1], rows) if trials else 0
        remaining = sum(not row["good"] for row in rows)
        if number == 1 and rows != trials[-1]:
            raise ValueError("capacity refactor changed ordinary request behavior")
        if number == 2 and not recovered:
            raise ValueError("intermediate-read patch fixed no covered requests")
        if number == 3 and remaining:
            raise ValueError("final patch leaves a covered request broken")
        summary.append(dict(stage=number, requests=len(rows),
                            recovered=recovered, remaining=remaining,
                            source_sha256=hashlib.sha256(source.encode()).hexdigest()))
        reports.append(str(report.relative_to(HERE)))
        trials.append(rows)

    out = Path(tempfile.mkdtemp(prefix="mtdchar-series-", dir=HERE / "results"))
    (out / "summary.json").write_text(json.dumps(dict(
        passed=True, source_pin=pin, stages=summary, source_reports=reports,
        limits=["Bounded C request matrix, not a proof over all requests",
                "Stages 1 and 2 retain documented baseline failures",
                "Final source is identical to the separately TLC-checked snapshot",
                "Independent admission-statistics defect is outside this split"]), indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    print("MTDCHAR SERIES CHECKS PASSED")
    print("Logs:", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
