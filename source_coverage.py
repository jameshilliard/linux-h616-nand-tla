# SPDX-License-Identifier: GPL-2.0-or-later
"""GCOV of extracted source bodies only; fixtures are deliberately excluded."""
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
    objects = sorted(p.name for p in directory.glob("*.gcno"))
    run = subprocess.run(["gcov", "--json-format", "--branch-probabilities", *objects],
                         cwd=directory, text=True, capture_output=True)
    (directory / "gcov.log").write_text(run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError("gcov failed; see gcov.log")
    files = {}
    for path in sorted(directory.glob("*.gcov.json.gz")):
        with gzip.open(path, "rt") as stream:
            report = json.load(stream)
        for entry in report["files"]:
            if entry["file"] in files:
                files[entry["file"]]["functions"].extend(entry["functions"])
                files[entry["file"]]["lines"].extend(entry["lines"])
            else:
                files[entry["file"]] = entry
    # Existing driver runners pass a list; the core runner supplies per-file
    # names. Never count fixture branches. Identical source helpers can be
    # extracted into both page and transport translation units; merge their
    # executions only when their source coordinates and branch shapes agree.
    by_file = names if isinstance(names, dict) else {"sunxi_nand.c": names}
    result = {}
    for filename, function_names in by_file.items():
        if filename not in files:
            raise RuntimeError("missing source-mapped coverage: " + filename)
        data = files[filename]
        for name in function_names:
            functions = [f for f in data["functions"] if f["name"] == name]
            if not functions:
                raise RuntimeError("missing source function coverage: " + name)
            locations = {tuple(f[k] for k in ("start_line", "start_column", "end_line", "end_column"))
                         for f in functions}
            if len(locations) != 1:
                raise RuntimeError("ambiguous source function coverage: " + name)
            function = functions[0]
            lines = [line for line in data["lines"] if line.get("function_name") == name]
            merged = {}
            for line in lines:
                number = line["line_number"]
                if number not in merged:
                    merged[number] = dict(line, branches=[dict(b) for b in line.get("branches", [])])
                    continue
                previous = merged[number]
                incoming = line.get("branches", [])
                if len(previous["branches"]) != len(incoming):
                    raise RuntimeError("inconsistent branch shape: " + name)
                previous["count"] += line["count"]
                for a, b in zip(previous["branches"], incoming):
                    if {k:v for k,v in a.items() if k != "count"} != {k:v for k,v in b.items() if k != "count"}:
                        raise RuntimeError("inconsistent branch identity: " + name)
                    a["count"] += b["count"]
            lines = list(merged.values())
            branches = [b for line in lines for b in line.get("branches", [])]
            key = f"{filename}:{name}" if isinstance(names, dict) else name
            result[key] = dict(calls=sum(f["execution_count"] for f in functions),
                extracted_copies=len(functions),
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
