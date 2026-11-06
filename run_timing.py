#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the complete sunxi timing solver against C observations and TLA+.

The clock provider is a fixture. Its no-faster-than-requested contract is
checked separately with a deliberate violating response, not assumed proved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from boundary_common import HERE, check, sources
from run_c import extract_function
from timing_cases import PROFILES, PROGRAM, divisors
import run as tlc

FIELDS = ("tCLS_min", "tCLH_min", "tCS_min", "tCH_min", "tWP_min",
          "tWH_min", "tALS_min", "tDS_min", "tDH_min", "tRR_min",
          "tALH_min", "tRP_min", "tREH_min", "tRC_min", "tWC_min",
          "tWB_max", "tADL_min", "tWHR_min", "tRHW_min", "tREA_max", "tRLOH_min")


def minimum(d, profile):
    p = max((n // c + bool(n % c)) for n, c in zip(d[:19], divisors(profile)))
    return max(p, d[19]) if not d[20] else p


def records():
    result = []
    for hw, profile in enumerate(PROFILES):
        def add(d, *, check_only=False, invalid=False, rate="requested", group="positive"):
            p = minimum(d, profile)
            requested = 10**9 // ((p + 999) // 1000)
            actual = requested if rate == "requested" else rate
            result.append(dict(profile=hw, d=tuple(d), rounded=actual,
                               check=check_only, invalid=invalid, group=group))

        # Each source constraint dominates, including one-ps ceiling boundaries.
        for field, divisor in enumerate(divisors(profile)):
            for period in (1000, 1001, 14999, 15000, 15001, 27000, 100001):
                for delta in (-1, 0, 1):
                    d = [0] * 21
                    d[4] = 1000
                    d[field] = period * divisor + delta
                    add(d)
        # Every table encoding, both sides of every threshold, including zeros.
        for slot, table in enumerate(profile["tables"]):
            for cycles in table:
                for delta in (-1, 0, 1):
                    d = [0] * 21
                    d[4] = 13001
                    d[slot + 15] = max(0, cycles * d[4] + delta)
                    add(d)
        for rea in (0, 14000, 14001, 50000):
            for hold in (0, 1):
                d = [1000] * 21
                d[4], d[19], d[20] = 14000, rea, hold
                p = minimum(d, profile)
                requested = 10**9 // ((p + 999) // 1000)
                for rate in (requested, requested // 2, 0, -22):
                    add(d, rate=rate)
                    add(d, rate=rate, check_only=True)
                add(d, invalid=True)
                add(d, invalid=True, check_only=True)
        # Reproducible multi-constraint vectors; not claimed exhaustive.
        seed = 1
        for _ in range(64):
            d = []
            for field in range(21):
                seed = (1664525 * seed + 1013904223) % (2**32)
                d.append(seed % 400001 if field < 20 else seed % 2)
            add(d)
        d = [0] * 21
        d[4], d[19] = 4000000, 4000001
        add(d)
        # An unsafe CCF response is an assumption counterexample, not a claim
        # that the sunxi clock provider actually returns this rate.
        d = [1000] * 21
        d[4], d[20] = 100000, 0
        add(d, rate=100000000, group="fast-provider")
    return result


def oracle(row):
    """Independent arbitrary-precision integer oracle for the C boundary."""
    d, profile = row["d"], PROFILES[row["profile"]]
    if row["invalid"]:
        return (-524, 291, 12345, 256, 0)
    period = minimum(d, profile)
    assert period > 0
    enc = [next(i for i, c in enumerate(t) if c * period >= n)
           for t, n in zip(profile["tables"], d[15:19])]
    if row["check"]:
        return (0, 291, 12345, 256, 0)
    cfg = sum(code * 4**i for i, code in enumerate(enc)) + 7 * 256
    rate = 10**9 // ((period + 999) // 1000)
    if row["rounded"] <= 0:
        # The source caches cfg/rate before a failed round_rate call. It does
        # not roll these fields back, and has not written hardware here.
        return (-22, cfg, rate, 256, 1)
    ns = 10**9 // row["rounded"]
    ctl = 256 if ns < 15 or ns * 1000 < d[19] else 0
    return (0, cfg, rate, ctl, 1)


def source_fixture(source, core_header, rows):
    definitions = []
    for name in ("SUNXI_NFC_TIMING_STEPS", "NFC_TIMING_CFG", "NFC_TIMING_CTL_EDO"):
        match = re.search(r"^#define " + name + r"\b[^\n\\]*(?:\\\n[^\n\\]*)*",
                          source, re.M)
        if not match:
            raise ValueError("missing register definition: " + name)
        definitions.append(match[0])
    for field in FIELDS:
        if not re.search(r"\bu32\s+" + field + r";", core_header):
            raise ValueError("timing fixture field width changed: " + field)
    definitions += [(HERE / "tests/timing.h").read_text()]
    match = re.search(r"struct sunxi_nfc_timings \{.*?\n};", source, re.S)
    if not match:
        raise ValueError("missing timing structure")
    definitions.append(match[0])
    for name, profile in zip(("sun4i_a10_nfc_timings", "sun50i_h616_nfc_timings"), PROFILES):
        body = re.search(r"static const struct sunxi_nfc_timings " + name + r" = \{.*?\n};",
                         source, re.S)[0]
        setup = int(re.search(r"\.setup_cycles\s*=\s*(\d+)", body)[1])
        tables = tuple(tuple(map(int, re.findall(r"\d+", re.search(
            r"\." + field + r"\s*=\s*\{(.*?)\}", body)[1])))
            for field in ("tWB", "tADL", "tWHR", "tRHW"))
        if setup != profile["setup"] or tables != profile["tables"]:
            raise ValueError("model timing profile differs from source: " + name)
        definitions.append(body)
    # Source helper contract: exactly the 21 u32 SDR fields modeled here.
    setup = extract_function(source, "sunxi_nfc_setup_interface")
    code = re.sub(r"/\*.*?\*/", "", setup, flags=re.S)
    used = set(re.findall(r"(?<!nfc_)timings->(t\w+)", code))
    if used != set(FIELDS):
        raise ValueError("modeled timing input set changed: " + repr(used))
    hashes = {}
    definitions.append("#define sunxi_nand_lookup_timing(l,p,c) "
                       "_sunxi_nand_lookup_timing(l,ARRAY_SIZE(l),p,c)")
    for name in ("_sunxi_nand_lookup_timing", "sunxi_nfc_setup_interface"):
        body = extract_function(source, name)
        line = source[:source.index(body)].count("\n") + 1
        definitions.append(f'#line {line} "sunxi_nand.c"\n' + body)
        hashes["sunxi_nand.c:" + name] = hashlib.sha256(body.encode()).hexdigest()
    definitions.append("static const struct timing_input inputs[] = {")
    for r in rows:
        definitions.append("{" + str(r["profile"]) + ",{" + ",".join(map(str, r["d"])) +
                           "}," + ",".join(str(int(r[k])) for k in
                                            ("rounded", "check", "invalid")) + "},")
    definitions += ["};", '#line 1 "timing.c"', (HERE / "tests/timing.c").read_text()]
    return "\n\n".join(definitions), hashes


def model_cases(rows):
    cases = []
    def add(name, profile, selected, *, mutation="none", expected=None, kind="positive"):
        trials = tuple({k: r[k] for k in ("d", "rounded", "check", "invalid", "observed")}
                       for r in selected)
        assert trials
        cases.append(dict(module="NandTimingProgram", name=name, kind=kind, expected=expected,
                          constants=dict(Divisors=divisors(profile), Tables=profile["tables"],
                                         Trials=trials, Mutation=mutation)))
    for hw, profile in enumerate(PROFILES):
        good = [r for r in rows if r["profile"] == hw and r["group"] == "positive"]
        add("timing-source-" + profile["name"], profile, good)
        add("timing-fast-provider-" + profile["name"], profile,
            [r for r in rows if r["profile"] == hw and r["group"] == "fast-provider"],
            kind="assumption", expected="ClockSafe")
    profile = PROFILES[1]
    good = [r for r in rows if r["profile"] == 1 and r["group"] == "positive"]
    for mutation, expected in (("floor-lookup", "LookupFits"),
                               ("write-check-only", "CheckOnlyPure"),
                               ("cad-overlap", "SourceAgreement"),
                               ("floor-ns", "ValidDivisors"),
                               ("edo-requested", "SourceAgreement")):
        add("timing-mutation-" + mutation, profile, good, mutation=mutation,
            kind="mutation", expected=expected)
    add("timing-mutation-floor-ns-nonzero", profile,
        [r for r in good if minimum(r["d"], profile) >= 1000],
        mutation="floor-ns", kind="mutation", expected="SourceAgreement")
    return cases


def validate_observations(rows, observed):
    assert len(observed) == len(rows)
    for serial, (row, obs) in enumerate(zip(rows, observed)):
        assert obs["id"] == serial
        row["observed"] = tuple(obs["observed"])
        assert row["observed"] == oracle(row), (serial, row, oracle(row))
        assert obs["requested"] == (row["observed"][2] if row["observed"][4] else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source", type=Path)
    parser.add_argument("--source-only", action="store_true")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--case", default="")
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    if not __debug__:
        parser.error("assertions must remain enabled")
    src = sources(args.candidate_source)
    rows = records()
    generated, hashes = source_fixture(src["sunxi_nand.c"], src["rawnand.h"], rows)
    out = check("timing", {"check.c": generated}, hashes,
                expected="ALL TIMING SOURCE CHECKS PASSED", artifacts=("timing.jsonl",),
                coverage=({"sunxi_nand.c": src["sunxi_nand.c"]},
                          {"sunxi_nand.c": ["_sunxi_nand_lookup_timing", "sunxi_nfc_setup_interface"]}),
                limits=["Clock rounding is an explicit fixture; not a physical clock proof.",
                        "The 21 input fields have their pinned kernel u32 types.",
                        "Source observations cover boundary/multi-constraint vectors, not all u32 inputs.",
                        "The separate NandTimingFold quotient explores every vector in its finite domains.",
                        "No hardware register access is performed by this setup helper."])
    raw = (out / "timing.jsonl").read_bytes()
    observed = [json.loads(line) for line in raw.splitlines()]
    validate_observations(rows, observed)
    report = json.loads((out / "summary.json").read_text())
    report.update(observations=len(rows), oracle_passed=True,
                  fixture_sha256={n: tlc.digest(HERE / n) for n in
                                  ("run_timing.py", "timing_cases.py", "tests/timing.h", "tests/timing.c")})
    (out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    if args.source_only:
        return 0
    return tlc.main(["--case", args.case, "--timeout", str(args.timeout)] +
                    (["--download"] if args.download else []), cases=model_cases(rows),
                    modules={"NandTimingProgram": PROGRAM}, metadata=dict(
                        driver_sha256=hashlib.sha256(src["sunxi_nand.c"].encode()).hexdigest(),
                        observations=len(rows), trace_sha256=hashlib.sha256(raw).hexdigest()))


if __name__ == "__main__":
    raise SystemExit(main())
