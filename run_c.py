#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile exact submitted C functions with bounded host-controller fixtures."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
FUNCTIONS = [
    "sunxi_nfc_user_data_to_buf", "sunxi_nfc_buf_to_user_data",
    "sunxi_nfc_user_data_sz", "sunxi_nfc_dma_batch_steps",
    "sunxi_get_oob_offset", "sunxi_get_ecc_offset",
    "sunxi_nand_ooblayout_ecc", "sunxi_nand_ooblayout_free",
    "sunxi_nfc_maximize_user_data", "sunxi_nfc_oob_min_user_data",
    "sunxi_nfc_h6_allwinner_oob_user_data_sz",
    "sunxi_nfc_allwinner_oob_min_user_data", "sunxi_nfc_allwinner_oob_init_user_data",
    "sunxi_nfc_user_data_len_code", "sunxi_nfc_reset_user_data_len",
    "sunxi_nfc_set_user_data_len_pio", "sunxi_nfc_set_user_data_len_dma",
    "sunxi_nfc_randomizer_step", "sunxi_nfc_randomizer_state",
    "sunxi_nfc_randomize_buf", "sunxi_nfc_randomize_bbm",
    "sun4i_a10_nfc_spare_is_erased", "sun50i_h6_nfc_spare_is_erased",
    "sunxi_nfc_allwinner_oob_spare_is_erased",
    "sunxi_nfc_hw_ecc_get_prot_oob_bytes", "sunxi_nfc_hw_ecc_set_prot_oob_bytes",
    "sunxi_nfc_hw_ecc_update_stats", "sunxi_nfc_read_column",
    "sunxi_nfc_hw_ecc_read_error",
    "sunxi_nfc_hw_ecc_read_pattern_id", "sunxi_nfc_hw_ecc_read_status",
    "sunxi_nfc_hw_ecc_read_count", "sunxi_nfc_hw_ecc_correct",
    "sunxi_nfc_hw_ecc_read_extra_oob", "sunxi_nfc_oob_read_batch",
    "sunxi_nfc_oob_finish_read", "sunxi_nfc_allwinner_oob_record_status",
    "sunxi_nfc_allwinner_oob_read_unprotected",
    "sunxi_nfc_allwinner_oob_read_batch", "sunxi_nfc_allwinner_oob_finish_read",
    "sunxi_nfc_hw_ecc_read_chunk", "sunxi_nfc_hw_ecc_read_batch_dma",
    "sunxi_nfc_hw_ecc_read_chunks_dma", "sunxi_nfc_hw_ecc_read_page",
    "sunxi_nfc_hw_ecc_read_page_dma", "sunxi_nfc_hw_ecc_read_subpage",
    "sunxi_nfc_hw_ecc_read_subpage_dma",
    "sunxi_nfc_hw_ecc_write_chunk", "sunxi_nfc_hw_ecc_write_extra_oob",
    "sunxi_nfc_hw_ecc_write_page", "sunxi_nfc_hw_ecc_write_subpage",
    "sunxi_nfc_hw_ecc_write_page_dma", "sunxi_nfc_hw_ecc_read_oob",
    "sunxi_nfc_hw_ecc_write_oob",
    "sunxi_nfc_oob_init_user_data", "sunxi_nand_hw_ecc_ctrl_init",
    "sunxi_nfc_allwinner_oob_read_subpage",
    "sunxi_nfc_allwinner_oob_attach_chip", "sunxi_nand_attach_chip",
]
STRUCTURES = [
    "sunxi_nfc_oob_ops", "sunxi_nfc_allwinner_oob_status",
    "sunxi_nfc_ecc_read", "sunxi_nfc_ecc_snapshot",
]
ARRAYS = ["sunxi_nfc_randomizer_page_seeds", "sunxi_nfc_randomizer_ecc512_seeds",
          "sunxi_nfc_randomizer_ecc1024_seeds", "sunxi_user_data_len_h6",
          "sunxi_ecc_strengths_a10", "sunxi_ecc_strengths_h6"]


def extract_function(source, name):
    match = re.search(r"^static [^;{}]*\b" + re.escape(name) +
                      r"\([^;{}]*\)\n\{", source, re.M)
    if not match:
        raise ValueError(f"function not found: {name}")
    return source[match.start():source.index("\n}\n", match.end()) + 3]


def chunk_functions(source):
    """Include the candidate refactor without changing the pinned v9 source."""
    return ["sunxi_nfc_buf_chunk_size"] if "sunxi_nfc_buf_chunk_size" in source else []


def chunk_defines(source):
    match = re.search(r"^#define NFC_MAX_BYTE_COUNT[^\n]*", source, re.M)
    return match[0] + "\n" if match else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--candidate-source", type=Path,
                        help="test this sunxi_nand.c without replacing the pinned reference")
    parser.add_argument("--oob-vectors", type=Path,
                        help="observe randomized-OOB classification/transport vectors")
    args = parser.parse_args()
    source = (HERE / "reference/sunxi_nand.c").read_text()
    pin = json.loads((HERE / "source.json").read_text())
    if hashlib.sha256(source.encode()).hexdigest() != pin["driver_sha256"]:
        parser.error("source hash mismatch")
    if args.candidate_source:
        source = args.candidate_source.read_text()
    binding = (HERE / "reference/allwinner,sun4i-a10-nand.yaml").read_bytes()
    if hashlib.sha256(binding).hexdigest() != pin["binding_sha256"]:
        parser.error("binding hash mismatch")
    generated = [chunk_defines(source),
                 "struct sunxi_nfc_ecc_read;\nstruct sunxi_nfc_ecc_snapshot;\n"]
    functions = chunk_functions(source) + FUNCTIONS
    hashes = {}
    for name in STRUCTURES:
        start = source.index("struct " + name + " {")
        generated.append(source[start:source.index("\n};", start) + 3])
    for name in ARRAYS:
        match = re.search(r"^static const u(?:8|16) " + name + r"\[\] = \{.*?\n};",
                          source, re.M | re.S)
        if not match:
            raise ValueError(name)
        generated.append(match[0])
    for name in functions:
        if name == "sunxi_nfc_allwinner_oob_attach_chip":
            for ops in ("sunxi_nfc_plain_oob_ops", "sunxi_nfc_scrambled_plain_bbm_oob_ops",
                        "sunxi_nfc_allwinner_oob_ops"):
                match = re.search(r"^static const struct sunxi_nfc_oob_ops " + ops +
                                  r" = \{.*?\n};", source, re.M | re.S)
                generated.append(match[0])
        function = extract_function(source, name)
        line = source[:source.index(function)].count("\n") + 1
        generated.append(f'#line {line} "sunxi_nand.c"\n' + function)
        hashes[name] = hashlib.sha256(function.encode()).hexdigest()
    results = HERE / "results"
    results.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="c-", dir=results))
    from compat_sources import generate
    compat, compat_hashes = generate(HERE)
    combined = (f'#define FIXED_SUNXI {int(bool(args.candidate_source))}\n' +
                f'#define STREAMING_RECOVERY {int("bool recovery_failed;" in source and "sunxi_nfc_dma_release(" not in source)}\n' +
                '#line 1 "fixtures.h"\n' + (HERE / "tests/fixtures.h").read_text() + "\n" +
                "\n".join(generated) + '\n#line 1 "compat-source.c"\n' + compat +
                '\n#line 1 "check-tests.c"\n' + (HERE / "tests/check.c").read_text() +
                '\n#line 1 "flow.c"\n' + (HERE / "tests/flow.c").read_text() +
                '\n#line 1 "attach.c"\n' + (HERE / "tests/attach.c").read_text() +
                '\n#line 1 "format.c"\n' + (HERE / "tests/format.c").read_text() +
                '\n#line 1 "compat.c"\n' + (HERE / "tests/compat.c").read_text() +
                '\n#line 1 "codec_bridge.c"\n' + (HERE / "tests/codec_bridge.c").read_text() +
                '\n#line 1 "oob_pipeline.c"\n' + (HERE / "tests/oob_pipeline.c").read_text())
    (out / "check.c").write_text(combined)
    command = [args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
               "-fno-strict-aliasing", "-fno-strict-overflow", "-funsigned-char",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               "-fno-omit-frame-pointer", "-no-pie", "check.c", "-o", "check"]
    build = subprocess.run(command, cwd=out, text=True, capture_output=True)
    (out / "build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        print(build.stdout + build.stderr)
        return 1
    vectors = args.oob_vectors.read_bytes() if args.oob_vectors else None
    if vectors is not None:
        (out / "oob-vectors.bin").write_bytes(vectors)
        os.environ["NAND_OOB_VECTORS"] = str(out / "oob-vectors.bin")
        os.environ["NAND_OOB_TRACE"] = str(out / "oob-traces.jsonl")
    else:
        os.environ.pop("NAND_OOB_VECTORS", None)
        os.environ.pop("NAND_OOB_TRACE", None)
    run = subprocess.run(["./check"], cwd=out, text=True, capture_output=True, timeout=180)
    (out / "check.log").write_text(run.stdout + run.stderr)
    passed = run.returncode == 0 and "ALL C CHECKS PASSED" in run.stdout and not run.stderr
    traces = (out / "oob-traces.jsonl").read_bytes() if passed and args.oob_vectors else None
    from source_coverage import build_and_collect
    coverage = build_and_collect(out, command, source, functions) if passed else None
    if traces is not None and (out / "oob-traces.jsonl").read_bytes() != traces:
        raise RuntimeError("ASan/UBSan and GCOV builds produced different OOB traces")
    (out / "summary.json").write_text(json.dumps(
        dict(passed=passed, driver_sha256=hashlib.sha256(source.encode()).hexdigest(),
             candidate=bool(args.candidate_source),
             functions=hashes, compatibility_functions=compat_hashes,
             output=run.stdout, diagnostics=run.stderr, coverage=coverage,
             oob_traces_sha256=hashlib.sha256(traces).hexdigest() if traces is not None else None,
             oob_trace_count=len(traces.splitlines()) if traces is not None else 0,
             oob_vectors_sha256=hashlib.sha256(vectors).hexdigest() if vectors is not None else None,
             fixture_sha256=hashlib.sha256(combined.encode()).hexdigest(),
             compiler=args.cc, compiler_flags=command[1:-4]), indent=2) + "\n")
    print(run.stdout + run.stderr)
    print("Logs:", out)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
