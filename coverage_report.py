#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Combine passed host-source GCOV reports and list unexecuted driver functions."""
import argparse
import json
import re
from pathlib import Path
from run_c import HERE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path, help="C-run summary.json files")
    args = parser.parse_args()
    source = (HERE / "reference/sunxi_nand.c").read_text()
    pin = json.loads((HERE / "source.json").read_text())
    functions = {}
    for path in args.reports:
        report = json.loads(path.read_text())
        if not report["passed"] or report["driver_sha256"] != pin["driver_sha256"]:
            parser.error("report is failed or refers to a different reference")
        for name, info in report["coverage"]["functions"].items():
            if ":" in name:
                filename,name=name.split(":",1)
                if filename!="sunxi_nand.c":
                    continue
            combined = functions.setdefault(name, dict(calls=0, lines=set(), hit=set(), branches=set(), branches_hit=set()))
            combined["calls"] += info["calls"]
            for dst, src in (("lines", "source_lines"), ("hit", "covered_lines"),
                             ("branches", "source_branches"), ("branches_hit", "covered_branches")):
                combined[dst].update(info[src])
    all_names = re.findall(r"^static [^;{}]*?\b(\w+)\([^;{}]*\)\n\{", source, re.M)
    result = dict(kind="bounded-host-fixture-coverage-not-real-kernel-coverage",
                  driver_sha256=pin["driver_sha256"])
    result["functions_total"] = len(all_names)
    result["functions_extracted"] = len(functions)
    result["functions_executed"] = sum(v["calls"] > 0 for v in functions.values())
    result["lines"] = sum(len(v["lines"]) for v in functions.values())
    result["lines_hit"] = sum(len(v["hit"]) for v in functions.values())
    result["branches"] = sum(len(v["branches"]) for v in functions.values())
    result["branches_hit"] = sum(len(v["branches_hit"]) for v in functions.values())
    result["unextracted_functions"] = [name for name in all_names if name not in functions]
    result["unexecuted_functions"] = [name for name, v in functions.items() if not v["calls"]]
    result["missed_lines"] = {name: sorted(v["lines"] - v["hit"]) for name,v in functions.items()
                              if v["lines"] - v["hit"]}
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
