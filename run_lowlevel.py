#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute unchanged DMA/IRQ/command/timing driver bodies with API fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from run_c import HERE, extract_function

FUNCTIONS = [
    "sunxi_nfc_interrupt", "sunxi_nfc_wait_events", "sunxi_nfc_wait_cmd_fifo_empty",
    "sunxi_nfc_rst", "sunxi_nfc_dma_op_prepare", "sunxi_nfc_dma_op_cleanup",
    "sunxi_nfc_dma_op_abort", "sunxi_nfc_select_chip", "sunxi_nfc_read_buf",
    "sunxi_nfc_write_buf", "sunxi_nfc_randomizer_step", "sunxi_nfc_randomizer_state",
    "sunxi_nfc_randomizer_config", "sunxi_nfc_randomizer_enable", "sunxi_nfc_randomizer_disable",
    "sunxi_nfc_randomizer_write_buf", "sunxi_nfc_randomizer_read_buf",
    "sunxi_nfc_hw_ecc_enable", "sunxi_nfc_hw_ecc_disable", "_sunxi_nand_lookup_timing",
    "sunxi_nfc_setup_interface", "sunxi_nfc_exec_subop", "sunxi_nfc_soft_waitrdy", "sunxi_nfc_exec_op",
    "sunxi_nfc_dma_init",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--candidate-source", type=Path,
                        help="sunxi_nand.c containing the split-address and clock-error fixes")
    parser.add_argument("--expect-count-fixed", action="store_true",
                        help="require exact large transfers and accesses within RAM0's 1 KiB bank")
    parser.add_argument("--recovery-traces", action="store_true",
                        help="record source-helper DMA/IRQ/abort/reuse boundaries for TLA checking")
    args = parser.parse_args()
    if args.expect_count_fixed and not args.candidate_source:
        parser.error("--expect-count-fixed requires --candidate-source")
    source = (HERE / "reference/sunxi_nand.c").read_text()
    pin = json.loads((HERE / "source.json").read_text())
    if hashlib.sha256(source.encode()).hexdigest() != pin["driver_sha256"]:
        parser.error("reference source hash mismatch")
    if args.candidate_source:
        source = args.candidate_source.read_text()
    core = (HERE / "reference/nand_parser.c").read_text()
    header = (HERE / "reference/nand_parser.h").read_text()
    for text, key in ((core, "parser_excerpt_sha256"), (header, "parser_header_excerpt_sha256")):
        if hashlib.sha256(text.encode()).hexdigest() != pin[key]:
            parser.error("core parser excerpt hash mismatch")
    generated = []
    for match in re.finditer(r"^#define (?:NFC_|NDFC_|SUNXI_NFC_TIMING_STEPS)[^\n\\]*(?:\\\n[^\n\\]*)*", source, re.M):
        generated.append(match[0])
    for name in ("sunxi_nfc_randomizer_page_seeds", "sunxi_nfc_randomizer_ecc512_seeds",
                 "sunxi_nfc_randomizer_ecc1024_seeds", "sun4i_a10_nfc_timings", "sun50i_h616_nfc_timings"):
        match = re.search(r"^static const [^;{}]*\b" + name + r"[^;{}]*= \{.*?\n};", source, re.M | re.S)
        if not match:
            raise ValueError(name)
        generated.append(match[0])
    # Keep each exercised capability expression tied to the submitted SoC data,
    # rather than reconstructing A10/A23/H616 values in the fixture.
    fields = ("has_mdma", "has_mdma_desc", "reg_spare_area", "random_en_mask",
              "reg_io_data", "dma_maxburst", "sram_size", "timings", "max_ecc_steps",
              "reg_ecc_err_cnt", "reg_user_data", "reg_user_data_len", "reg_pat_found",
              "random_dir_mask", "ecc_mode_mask", "ecc_err_mask", "pat_found_mask",
              "has_ecc_block_512")
    generated.append("static const struct sunxi_nfc_caps source_caps[] = {")
    for soc in ("a10", "a23", "h616"):
        match = re.search(r"^static const struct sunxi_nfc_caps sunxi_nfc_" + soc +
                          r"_caps = \{(.*?)\n};", source, re.M | re.S)
        if not match:
            raise ValueError("missing SoC capabilities: " + soc)
        values = []
        for field in fields:
            value = re.search(r"^\s*\." + field + r"\s*=\s*(.*),$", match[1], re.M)
            if value:
                values.append(f".{field} = {value[1]}")
        generated.append("{ " + ", ".join(values) + " },")
    generated.append("};")
    hashes = {}
    for name in FUNCTIONS:
        if name == "sunxi_nfc_exec_op":
            start = source.index("static const struct nand_op_parser sunxi_nfc_op_parser")
            end = source.index("static int sunxi_nfc_exec_op", start)
            generated.append(source[start:end])
        body = extract_function(source, name)
        line = source[:source.index(body)].count("\n") + 1
        generated.append(f'#line {line} "sunxi_nand.c"\n' + body)
        hashes[name] = hashlib.sha256(body.encode()).hexdigest()
    results = HERE / "results"
    results.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="lowlevel-", dir=results))
    fixture_inputs = {name: (HERE / "tests" / name).read_text()
                      for name in ("lowlevel.h", "lowlevel.c")}
    fixture = fixture_inputs["lowlevel.h"].replace("/* @parser-header@ */", header)
    tests = fixture_inputs["lowlevel.c"]
    if args.recovery_traces:
        fixture_inputs["recovery_trace.c"] = (HERE / "tests/recovery_trace.c").read_text()
        tests = tests.replace("/* @recovery-traces@ */", fixture_inputs["recovery_trace.c"])
    combined = (f'#define FIXED_SUNXI {int(bool(args.candidate_source))}\n' +
                f'#define FIXED_COUNT {int(args.expect_count_fixed)}\n' +
                f'#define RECOVERY_TRACES {int(args.recovery_traces)}\n' +
                '#line 1 "lowlevel.h"\n' + fixture + '\n#line 1 "nand_parser.c"\n' + core + "\n" +
                "\n".join(generated) + '\n#line 1 "lowlevel.c"\n' +
                tests)
    (out / "check.c").write_text(combined)
    command = [args.cc, "-std=gnu11", "-O0", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
               "-fno-strict-aliasing", "-fno-strict-overflow", "-funsigned-char",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
               "check.c", "-o", "check"]
    build = subprocess.run(command, cwd=out, text=True, capture_output=True)
    (out / "build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        print(build.stdout + build.stderr)
        return 1
    run = subprocess.run(["./check"], cwd=out, text=True, capture_output=True, timeout=180)
    (out / "check.log").write_text(run.stdout + run.stderr)
    passed = run.returncode == 0 and "ALL LOWLEVEL CHECKS PASSED" in run.stdout and not run.stderr
    traces = (out / "recovery-traces.jsonl").read_bytes() if passed and args.recovery_traces else None
    from source_coverage import build_and_collect
    coverage = build_and_collect(out, command, source, FUNCTIONS) if passed else None
    if traces is not None and (out / "recovery-traces.jsonl").read_bytes() != traces:
        raise RuntimeError("sanitized and coverage builds produced different recovery traces")
    (out / "summary.json").write_text(json.dumps(dict(
        passed=passed, driver_sha256=hashlib.sha256(source.encode()).hexdigest(),
        candidate=bool(args.candidate_source), count_fix=args.expect_count_fixed, functions=hashes,
        output=run.stdout, diagnostics=run.stderr, coverage=coverage,
        fixture_sha256={name: hashlib.sha256(text.encode()).hexdigest()
                        for name, text in fixture_inputs.items()},
        recovery_traces_sha256=hashlib.sha256(traces).hexdigest() if traces is not None else None,
        recovery_trace_count=len(traces.splitlines()) if traces is not None else 0), indent=2) + "\n")
    print(run.stdout + run.stderr)
    print("Logs:", out)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
