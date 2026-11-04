#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check source-executed lifecycle traces against a separate TLA+ protocol."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

import run as tlc
from probe_trace_cases import MODEL, KINDS, case, controls


def validate(trace):
    n = trace["children"]
    assert type(n) is int and 0 <= n <= 3
    ids = set(range(1, n + 1))
    assert 0 <= trace["mask"] < 2**n and trace["variant"] in range(4 if n else 1)
    assert type(trace["fault"]) is int and 0 <= trace["fault"] < 256
    assert type(trace["faultable_events"]) is int and 0 <= trace["faultable_events"] < 256
    assert set(trace["available"]) <= ids
    assert len(trace["available"]) == len(set(trace["available"]))
    assert trace["events"] and trace["events"][-1]["kind"] == "finish"
    for event in trace["events"]:
        assert set(event) == {"kind", "chip", "listed", "published"}
        assert event["kind"] in KINDS and type(event["chip"]) is int
        assert event["chip"] in ids | {0}
        for field in ("listed", "published"):
            assert set(event[field]) <= ids and len(event[field]) == len(set(event[field]))
    results = [e["kind"] for e in trace["events"] if e["kind"].startswith("probe-")]
    assert results == ["probe-error" if trace["returncode"] else "probe-ok"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, help="run_lifecycle.py --probe-traces result directory")
    parser.add_argument("--expect-baseline", action="store_true",
                        help="require the unchanged source's early-publication counterexample")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    summary = json.loads((args.results / "summary.json").read_text())
    raw = (args.results / "probe-traces.jsonl").read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if not summary["passed"] or digest != summary["probe_traces_sha256"]:
        parser.error("lifecycle execution failed or trace digest changed")
    traces = [json.loads(line) for line in raw.splitlines()]
    if not traces or len(traces) != summary["probe_trace_count"]:
        parser.error("trace count mismatch")
    for trace in traces:
        validate(trace)
    inputs = {}
    for trace in traces:
        key = tuple(trace[k] for k in ("children", "mask", "variant"))
        faults = inputs.setdefault(key, {})
        assert trace["fault"] not in faults, "duplicate source execution"
        faults[trace["fault"]] = trace
    assert set(inputs) == {(n, mask, variant) for n in range(4) for mask in range(2**n)
                           for variant in range(4 if n else 1)}, "missing input profile"
    for faults in inputs.values():
        assert 0 in faults
        assert set(faults) == set(range(faults[0]["faultable_events"] + 1)), "missing injected fault"
    # Faults at different internal allocation/negotiation sites may have the
    # same boundary trace. Every raw trace is accounted for, but TLC need not
    # reparse identical projections. Bound each wrapper's AST/heap footprint.
    projections, groups, counts = [], {}, []
    for trace in traces:
        projection = {key: trace[key] for key in ("available", "events")}
        key = json.dumps(projection, sort_keys=True, separators=(",", ":"))
        if key not in groups:
            groups[key] = len(projections)
            projections.append(projection)
            counts.append(0)
        counts[groups[key]] += 1
    assert sum(counts) == len(traces)
    fixed = summary["filtered_children"] and summary["deferred_publication"]
    if fixed == args.expect_baseline:
        parser.error("select fixed candidate traces, or --expect-baseline for the old source")
    if args.expect_baseline:
        if summary["filtered_children"] or summary["deferred_publication"]:
            parser.error("baseline mode requires neither candidate expectation")
        witness = next(t for t in traces if (t["children"], t["mask"], t["variant"], t["fault"]) == (2, 3, 0, 0))
        tests = [case("baseline", [witness], "AllScannedBeforePublish", "source-contract")]
    else:
        exemplar = next(t for t in traces if (t["children"], t["mask"], t["variant"], t["fault"]) == (2, 3, 0, 0))
        assert {e["kind"] for t in traces for e in t["events"]} == KINDS, "missing observation boundary"
        tests = [case(f"candidate-{i // 32:03d}", projections[i:i + 32])
                 for i in range(0, len(projections), 32)] + controls(exemplar)
    print(f"Recorded {len(traces)} traces, {len(projections)} distinct boundary projections; "
          f"driver {summary['driver_sha256']}", flush=True)
    return tlc.main(["--timeout", str(args.timeout)], cases=tests,
                    modules={"NandProbeTrace": MODEL}, metadata=dict(
                        source_sha256=summary["source_sha256"],
                        functions=summary["functions"], fixture_sha256=summary["fixture_sha256"],
                        traces_sha256=digest,
                        trace_count=len(traces), distinct_projection_count=len(projections),
                        input_profile_count=len(inputs),
                        observation_counts=dict(sorted(Counter(
                            e["kind"] for t in traces for e in t["events"]).items())),
                        projection_counts=counts, baseline=args.expect_baseline,
                        checked_baseline_input=dict(children=2, mask=3, variant=0, fault=0)
                        if args.expect_baseline else None))


if __name__ == "__main__":
    raise SystemExit(main())
