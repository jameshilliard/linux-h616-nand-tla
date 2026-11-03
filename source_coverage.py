# SPDX-License-Identifier: GPL-2.0-or-later
"""GCOV of extracted driver bodies only; fixtures are deliberately excluded."""
import gzip
import json
import subprocess


def build_and_collect(directory, command, source, names):
    # Separate build: sanitizer-generated branches are NOT source branches.
    coverage_command = [arg for arg in command if not arg.startswith(
        ("-fsanitize", "-fno-sanitize", "-O"))]
    coverage_command[1:1] = ["-O0", "--coverage"]
    build = subprocess.run(coverage_command, cwd=directory, text=True, capture_output=True)
    (directory / "coverage-build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        raise RuntimeError("coverage build failed")
    run = subprocess.run(["./check"], cwd=directory, text=True, capture_output=True, timeout=180)
    (directory / "coverage-check.log").write_text(run.stdout + run.stderr)
    if run.returncode or run.stderr or "CHECKS PASSED" not in run.stdout:
        raise RuntimeError("coverage executable failed")
    return collect(directory, source, names)


def collect(directory, source, names):
    run = subprocess.run(["gcov", "--json-format", "--branch-probabilities", "check.c"],
                         cwd=directory, text=True, capture_output=True)
    (directory / "gcov.log").write_text(run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError("gcov failed; see gcov.log")
    with gzip.open(directory / "check.gcov.json.gz", "rt") as stream:
        report = json.load(stream)
    files = [f for f in report["files"] if f["file"] == "sunxi_nand.c"]
    if len(files) != 1:
        raise RuntimeError("missing source-mapped coverage")
    data = files[0]
    result = {}
    for name in names:
        functions = [f for f in data["functions"] if f["name"] == name]
        if len(functions) != 1:
            raise RuntimeError("missing source function coverage: " + name)
        function = functions[0]
        lines = [line for line in data["lines"] if line.get("function_name") == name]
        branches = [b for line in lines for b in line.get("branches", [])]
        result[name] = dict(calls=function["execution_count"],
            lines=len(lines), lines_hit=sum(line["count"] > 0 for line in lines),
            branches=len(branches), branches_hit=sum(b["count"] > 0 for b in branches),
            missed_lines=[line["line_number"] for line in lines if not line["count"]],
            source_lines=[line["line_number"] for line in lines],
            covered_lines=[line["line_number"] for line in lines if line["count"]],
            source_branches=[f"{line['line_number']}:{i}" for line in lines
                             for i, branch in enumerate(line.get("branches", []))],
            covered_branches=[f"{line['line_number']}:{i}" for line in lines
                              for i, branch in enumerate(line.get("branches", [])) if branch["count"]])
    return dict(kind="host-source-gcov-not-kernel-coverage", functions=result,
                totals={k: sum(v[k] for v in result.values())
                        for k in ("lines", "lines_hit", "branches", "branches_hit")})
