#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run TLC exhaustively; a crash, parse error or timeout is never a passing test."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import urllib.request

from cases import CASES, MODULES

HERE = Path(__file__).resolve().parent
SOURCE = json.loads((HERE / "source.json").read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def render(value):
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, tuple):
        return "<<" + ", ".join(map(render, value)) + ">>"
    if isinstance(value, dict):
        return "[" + ", ".join(k + " |-> " + render(v) for k, v in value.items()) + "]"
    if isinstance(value, set):
        return "{" + ", ".join(render(v) for v in sorted(value)) + "}"
    raise TypeError(value)


def invariant_counterexample(returncode, log, name):
    """Accept the named invariant only, including an invalid initial state."""
    if returncode != 12:
        return False
    return ((f"Invariant {name} is violated." in log and
             "The behavior up to this point is:" in log) or
            (f"Invariant {name} is violated by the initial state:" in log and
             'CounterExample written: trace' in log))


def temporal_counterexample(returncode, log, name, *, unnamed=False):
    """Require a liveness counterexample, not a deadlock or a tool failure."""
    diagnostic = f"Temporal property {name} was violated." in log
    # TLC 1.7.4 does not print the property's name. The generated configuration
    # contains exactly one PROPERTY, so its generic diagnostic is unambiguous.
    if unnamed:
        diagnostic |= "Temporal properties were violated." in log
    return (returncode == 13 and diagnostic and
            "The following behavior constitutes a counter-example:" in log)


def main(argv=None, *, cases=CASES, modules=MODULES, metadata=None, toolchain=None):
    toolchain = SOURCE if toolchain is None else toolchain
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jar", type=Path, default=Path(os.environ.get(
        "TLA2TOOLS_JAR", HERE / ".tools" / toolchain.get("tlc_cache_file", "tla2tools.jar"))))
    parser.add_argument("--download", action="store_true",
                        help="download the pinned TLC jar if absent")
    parser.add_argument("--case", default="", help="substring filter")
    parser.add_argument("--timeout", type=int, default=180, help="seconds per case")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args(argv)
    selected = [c for c in cases if args.case in c["name"]]
    if not selected:
        parser.error("no matching cases")
    if args.list:
        for case in selected:
            print(case["name"], case["kind"], case["expected"] or "no errors")
        return 0
    if digest(HERE / "reference/sunxi_nand.c") != SOURCE["driver_sha256"]:
        parser.error("reference driver differs from the pinned submission")
    if digest(HERE / "reference/allwinner,sun4i-a10-nand.yaml") != SOURCE["binding_sha256"]:
        parser.error("reference binding differs from the pinned submission")
    for name, info in SOURCE["core_sources"].items():
        if digest(HERE / "reference" / name) != info["sha256"]:
            parser.error("reference core source differs from the pinned submission: " + name)
    jar = args.jar.resolve()
    if not jar.exists() and args.download:
        jar.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(toolchain["tlc_url"], timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != toolchain["tlc_sha256"]:
            parser.error("downloaded jar checksum mismatch")
        jar.write_bytes(data)
    if not jar.exists() or digest(jar) != toolchain["tlc_sha256"]:
        parser.error("TLC jar missing or checksum mismatch; use --download or --jar")
    results = HERE / "results"
    results.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="tlc-", dir=results))
    # A run must not mix model revisions if the workspace changes while TLC
    # is working. Record the bytes actually checked, not the files at exit.
    model_inputs = {p.name: p.read_bytes() for p in (HERE / "models").glob("*.tla")}
    summary = []
    reached = {m: set() for m in modules}
    for case in selected:
        module = case["module"]
        spec = modules[module]
        case_dir = out / case["name"]
        case_dir.mkdir()
        (case_dir / (module + ".tla")).write_bytes(model_inputs[module + ".tla"])
        for dependency in spec.get("dependencies", ()):
            (case_dir / (dependency + ".tla")).write_bytes(model_inputs[dependency + ".tla"])
        wrapper = "Check" + module
        definitions, assignments = [], []
        for key, value in case["constants"].items():
            definitions.append(f"C_{key} == {render(value)}")
            assignments.append(f"  {key} <- C_{key}")
        (case_dir / (wrapper + ".tla")).write_text(
            f"---- MODULE {wrapper} ----\nEXTENDS {module}\n" +
            "\n".join(definitions) + "\n====\n")
        temporal = case.get("temporal", False)
        invariants = [case["expected"]] if case["expected"] and not temporal else spec["invariants"]
        cfg = ("SPECIFICATION Spec\nCONSTANTS\n" + "\n".join(assignments) +
               "\nINVARIANTS\n  " + "\n  ".join(invariants) + "\n")
        if temporal:
            cfg += "PROPERTY " + case["expected"] + "\n"
        elif not case["expected"]:
            cfg += "PROPERTY Terminates\n"
        (case_dir / "model.cfg").write_text(cfg)
        command = ["java", "-Xmx1g", "-XX:+UseParallelGC", "-cp", str(jar),
                   "tlc2.TLC", "-workers", "1", "-seed", "1", "-fp", "0",
                   "-coverage", "1"]
        if toolchain.get("tlc_json_traces", True):
            command += ["-dumpTrace", "json", "trace"]
        command += ["-metadir", "states", "-config", "model.cfg", wrapper + ".tla"]
        start = time.monotonic()
        try:
            run = subprocess.run(command, cwd=case_dir, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 timeout=args.timeout)
            log, rc = run.stdout, run.returncode
        except subprocess.TimeoutExpired as error:
            raw = error.stdout or b""
            log = raw.decode(errors="replace") if isinstance(raw, bytes) else raw
            log += "\nHARNESS TIMEOUT\n"
            rc = -1
        (case_dir / "tlc.log").write_text(log)
        if temporal:
            matched = temporal_counterexample(rc, log, case["expected"],
                unnamed=toolchain["tlc_version"] == "1.7.4")
        elif case["expected"]:
            matched = invariant_counterexample(rc, log, case["expected"])
        else:
            matched = rc == 0 and "Model checking completed. No error has been found." in log
            if matched:
                for action, distinct, total in re.findall(
                        r"<(\w+) line [^>]+>:\s*(\d+)(?::(\d+))?", log):
                    # An enabled action may lead only to already-known states.
                    if int(total or distinct) and action in spec["actions"]:
                        reached[module].add(action)
        states = re.search(r"([\d,]+) states generated, ([\d,]+) distinct states found", log)
        record = dict(name=case["name"], module=module, kind=case["kind"],
                      expected=case["expected"], matched=matched, returncode=rc,
                      seconds=round(time.monotonic() - start, 3),
                      states=[int(x.replace(",", "")) for x in states.groups()] if states else None)
        summary.append(record)
        print(f"{'PASS' if matched else 'FAIL'} {case['name']} "
              f"({record['seconds']}s)", flush=True)
        if not matched:
            error = log.find("Error:")
            print(log[error:error + 3500] if error >= 0 else log[-3500:], flush=True)
    coverage = {}
    for module, spec in modules.items():
        positive = {c["name"] for c in cases if c["module"] == module and c["kind"] == "positive"}
        selected_names = {c["name"] for c in selected}
        missing = sorted(set(spec["actions"]) - reached[module])
        coverage[module] = dict(enforced=bool(positive) and positive <= selected_names,
                                reached=sorted(reached[module]), missing=missing)
    passed = all(r["matched"] for r in summary)
    coverage_ok = all(not c["enforced"] or not c["missing"] for c in coverage.values())
    report = dict(driver_sha256=SOURCE["driver_sha256"], tlc_sha256=toolchain["tlc_sha256"],
                  tlc_version=toolchain["tlc_version"],
                  results=summary, coverage=coverage, passed=passed and coverage_ok,
                  model_sha256={name: hashlib.sha256(data).hexdigest()
                                for name, data in model_inputs.items()})
    if metadata is not None:
        report["input_metadata"] = metadata
    (out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    for module, cov in coverage.items():
        if cov["enforced"] and cov["missing"]:
            print("FAIL uncovered actions", module, cov["missing"])
    print(f"{sum(r['matched'] for r in summary)}/{len(summary)} expectations matched; "
          f"logs: {out}")
    return 0 if passed and coverage_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
