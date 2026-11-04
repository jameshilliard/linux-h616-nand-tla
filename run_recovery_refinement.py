#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check source-helper recovery traces without presuming hardware retirement."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

import run as tlc
from recovery_trace_cases import MODEL, KINDS, case, controls


def validate(trace):
    domains = dict(backend=range(3), direction=range(2), schedule=range(7),
                   clear=range(2), nextCs=range(2), contract=range(5))
    assert set(trace) == set(domains) | {"events"}
    for field, domain in domains.items():
        assert type(trace[field]) is int and trace[field] in domain, field
    if trace["contract"]:
        assert trace["schedule"] == (0 if trace["contract"] == 4 else 6)
    assert trace["events"] and trace["events"][-1]["kind"] == "finish"
    boolean = {"mapped", "active", "bus", "complete", "disabled", "ram", "unsafe"}
    integer = {"arg", "mask", "status", "maps", "unmaps"}
    for event in trace["events"]:
        assert set(event) == boolean | integer | {"kind", "context"}
        assert event["kind"] in KINDS
        assert all(type(event[k]) is bool for k in boolean)
        assert all(type(event[k]) is int for k in integer)
        assert 0 <= event["mask"] <= 7 and 0 <= event["status"] <= 7
        assert 0 <= event["unmaps"] <= 2 and 0 <= event["maps"] <= 2
        assert len(event["context"]) == 6
        assert all(type(n) is int and 0 <= n < 2**31 for n in event["context"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, help="run_lowlevel.py --recovery-traces result directory")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--download", action="store_true", help="download the pinned TLC jar if absent")
    parser.add_argument("--case", default="", help="substring filter (full matrix checked without it)")
    args = parser.parse_args()
    summary = json.loads((args.results / "summary.json").read_text())
    raw = (args.results / "recovery-traces.jsonl").read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if not summary["passed"] or digest != summary["recovery_traces_sha256"]:
        parser.error("source execution failed or trace digest changed")
    traces = [json.loads(line) for line in raw.splitlines()]
    assert len(traces) == summary["recovery_trace_count"] == 264
    keys = set()
    for trace in traces:
        validate(trace)
        key = tuple(trace[k] for k in ("backend", "direction", "clear", "nextCs", "contract", "schedule"))
        assert key not in keys, "duplicate input profile"
        keys.add(key)
    expected = {(b, d, clear, cs, contract, schedule)
                for b in range(3) for d in range(2) for clear in range(2) for cs in range(2)
                for contract in range(5)
                for schedule in (range(7) if not contract else [0 if contract == 4 else 6])}
    assert keys == expected, "missing input profile"
    assert {e["kind"] for t in traces for e in t["events"]} == KINDS

    # Every trace is checked, including every conditional negative's complete
    # protocol and expected endpoint. Dedicated counterexamples additionally
    # demonstrate the exact lost safety property for each backend/contract.
    projections, groups, counts = [], {}, []
    for trace in traces:
        projection = {k: trace[k] for k in ("backend", "direction", "nextCs", "contract", "events")}
        key = json.dumps(projection, sort_keys=True, separators=(",", ":"))
        if key not in groups:
            groups[key] = len(projections)
            projections.append(projection)
            counts.append(0)
        counts[groups[key]] += 1
    assert sum(counts) == len(traces)
    tests = [case(f"integrity-{i // 2:03d}", projections[i:i + 2])
             for i in range(0, len(projections), 2)]
    for backend in range(3):
        for contract in range(1, 5):
            witness = next(t for t in traces if t["backend"] == backend and t["contract"] == contract)
            tests.append(case(f"contract-{backend}-{contract}", [witness],
                              "FreshCompletion" if contract == 3 else "MappingSafe", "assumption"))
    good = next(t for t in traces if t["backend"] == 2 and t["contract"] == 0 and t["schedule"] == 1)
    timeout = next(t for t in traces if t["backend"] == 2 and t["contract"] == 0 and t["schedule"] == 6)
    tests += controls(good, timeout)
    print(f"Recorded {len(traces)} traces, {len(projections)} distinct projections; "
          f"driver {summary['driver_sha256']}", flush=True)
    options = ["--timeout", str(args.timeout), "--case", args.case]
    if args.download:
        options.append("--download")
    return tlc.main(options, cases=tests,
                    modules={"NandRecoveryTrace": MODEL}, metadata=dict(
                        source_sha256=summary["driver_sha256"], functions=summary["functions"],
                        fixture_sha256=summary["fixture_sha256"], traces_sha256=digest,
                        trace_count=len(traces), distinct_projection_count=len(projections),
                        input_profile_count=len(keys), projection_counts=counts,
                        observation_counts=dict(sorted(Counter(
                            e["kind"] for t in traces for e in t["events"]).items()))))


if __name__ == "__main__":
    raise SystemExit(main())
