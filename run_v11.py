#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check both exact v11 source snapshots, not just the policy model."""
import argparse
from functools import cache
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile

from run_core import extract
from run_marker_admission import declaration

ROOT = Path(__file__).resolve().parent
REF = ROOT / "reference/sunxi-v11"
ORDINARY = "555426a171ec7993ac5cb38f16b00d5443bda9ab5b649347c402fa1d5381313f"
FIRMWARE = "b157ebf3c1d149f7fa3c395b3160f65252410bd0142056faac249b24062ea247"
CORE = "efcbf598df9bde05a304366e75e0bd1126ae002155cfe3d03dad0f4685ce807d"
NAND_BASE = "b0850d52b1e3d0ebf76d4ced5e8c6e1540ffe712fbbdea75ba72af5e042b282f"


def checked(path, digest):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"{path.name}: reference hash mismatch")
    return data.decode()


def pinned_pair(tmp):
    ordinary, firmware = REF / "sunxi_nand.c", tmp / "sunxi_nand.c"
    checked(ordinary, ORDINARY)
    shutil.copyfile(ordinary, firmware)
    subprocess.run(["git", "apply", "--no-index", "--whitespace=error", "-p5", REF / "firmware.patch"],
                   cwd=tmp, check=True)
    checked(firmware, FIRMWARE)
    return ordinary, firmware


@cache
def pinned_nand_base():
    # Older suites deliberately exercise the historical core. Only v11 uses
    # the delta; verify the complete reconstructed file before extracting C.
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        target = tmp / "nand_base.c"
        shutil.copyfile(ROOT / "reference/nand_base.c", target)
        subprocess.run(["git", "apply", "--no-index", "--unidiff-zero", "--whitespace=error",
                        REF / "nand-base.patch"], cwd=tmp, check=True)
        return checked(target, NAND_BASE)


def test_driver(driver, cc, out, expected_failure=None, *, vectors=None):
    core = checked(REF / "nand-core.c", CORE)
    nand = pinned_nand_base()
    header = (ROOT / "reference/rawnand.h").read_text()
    vendor = "sunxi_nfc_allwinner_oob_record_status(" in driver
    lines = [(ROOT / "tests/v11.h").read_text(), f"#define HAS_FW {int(vendor)}"]
    functions = {}

    def add(name):
        body = extract(driver, name)
        functions[name] = hashlib.sha256(body.encode()).hexdigest()
        lines.append(body)

    if vectors is not None:
        if not vendor:
            raise ValueError("the firmware byte pipeline needs a deferred-format driver")
        lines.append("#define V11_OOB_PIPELINE 1")
        lines.append(re.search(r"^#define SUNXI_NFC_H6_ALLWINNER_OOB_MAX_USER_DATA_SZ \d+", driver, re.M)[0])
    for name in ("NAND_BBM_FIRSTPAGE", "NAND_BBM_SECONDPAGE", "NAND_BBM_LASTPAGE"):
        lines.append(re.search(r"^#define " + name + r"\s+.+", header, re.M)[0])
    for name in ("SUNXI_NFC_BBM_SIZE", "NFC_MAX_BYTE_COUNT"):
        lines.append(re.search(r"^#define " + name + r"\s+\d+", driver, re.M)[0])
    for prefix in ("page", "ecc512", "ecc1024"):
        lines.append(declaration(driver, "static const u16 sunxi_nfc_randomizer_" + prefix + "_seeds[] = {"))
    if vendor:
        lines.append(declaration(driver, "struct sunxi_nfc_allwinner_oob_status {"))
    for name in ("ecc_read", "ecc_snapshot"):
        lines.append(declaration(driver, "struct sunxi_nfc_" + name + " {"))
    lines += [extract(core, n) for n in ("nand_check_erased_buf", "nand_check_erased_ecc_chunk")]
    lines += [extract(nand, n) for n in ("nand_bbm_get_next_page", "nand_block_bad")]
    common = """randomizer_step randomizer_state randomize_bbm user_data_to_buf
              user_data_before user_data_sz hw_ecc_get_prot_oob_bytes hw_ecc_update_stats
              read_column hw_ecc_read_error hw_ecc_read_pattern_id hw_ecc_read_count
              hw_ecc_read_extra_oob reset_user_data_len user_data_len_code
              set_user_data_len_pio set_user_data_len_dma""".split()
    # get_oob_offset and get_ecc_offset deliberately have no nfc_ prefix.
    for n in common[:5]:
        add("sunxi_nfc_" + n)
    for n in ("sunxi_get_oob_offset", "sunxi_get_ecc_offset"):
        add(n)
    if vendor:
        add("sunxi_nfc_bbm_needs_compensation")
    for n in common[5:]:
        add("sunxi_nfc_" + n)
    if vendor:
        extra = """sunxi_nfc_buf_chunk_size sunxi_nfc_randomize_buf sunxi_nfc_allwinner_oob_record_status
                   sun4i_a10_nfc_spare_is_erased sun50i_h6_nfc_spare_is_erased
                   sunxi_nfc_allwinner_oob_spare_is_erased sunxi_nfc_allwinner_oob_read_unprotected
                   sunxi_nfc_allwinner_oob_read_batch sunxi_nfc_allwinner_oob_finish_read""".split()
        for n in extra:
            add(n)
    for n in ("oob_read_batch", "oob_finish_read", "hw_ecc_read_status"):
        add("sunxi_nfc_" + n)
    if vectors is None:
        for n in ("dma_batch_steps", "hw_ecc_read_batch_dma", "hw_ecc_read_chunks_dma"):
            add("sunxi_nfc_" + n)
        add("sunxi_nfc_hw_ecc_read_chunk")
        if "sunxi_nfc_hw_ecc_read_chunks_pio(" in driver:
            add("sunxi_nfc_hw_ecc_read_chunks_pio")
        for n in ("read_page", "read_subpage", "read_page_dma", "read_subpage_dma"):
            add("sunxi_nfc_hw_ecc_" + n)
        if vendor:
            add("sunxi_nfc_allwinner_oob_read_subpage")
        for n in ("buf_to_user_data", "hw_ecc_set_prot_oob_bytes",
                  "hw_ecc_write_chunk", "hw_ecc_write_extra_oob", "hw_ecc_write_page",
                  "hw_ecc_write_subpage", "hw_ecc_write_page_dma", "hw_ecc_write_oob"):
            add("sunxi_nfc_" + n)
        add("sunxi_nand_ooblayout_free")
        lines.append('#include "v11_write_core.h"')
        refs = json.loads((ROOT / "source.json").read_text())["core_sources"]
        for file, names in (("mtd.h", ["mtd_oobavail"]),
                            ("mtdcore.c", ["mtd_ooblayout_free", "mtd_ooblayout_find_region",
                                           "mtd_ooblayout_set_bytes", "mtd_ooblayout_set_databytes"])):
            source = checked(ROOT / "reference" / file, refs[file]["sha256"])
            lines.extend(extract(source, name) for name in names)
        lines.append(re.search(r"^#define NOTALIGNED\(x\).+", nand, re.M)[0])
        lines.extend(extract(nand, name) for name in ("nand_fill_oob", "nand_write_page", "nand_do_write_ops"))
        lines.append(extract((ROOT / "tests/flow.c").read_text(), "write_request"))
        lines.append((ROOT / "tests/v11.c").read_text())
    else:
        for n in ("buf_to_user_data", "dma_batch_steps", "allwinner_prefix_size",
                  "allwinner_init_prefix"):
            add("sunxi_nfc_" + n)
        helpers = (ROOT / "tests/check.c").read_text()
        lines += [extract(helpers, n) for n in ("caps", "chip", "load_status")]
        lines.append((ROOT / "tests/oob_pipeline.c").read_text())
        lines.append("int main(void) { oob_pipeline(); return 0; }")
    unit = "\n".join(lines)
    exe = out / "policy"
    subprocess.run([cc, "-std=gnu11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable",
                    "-Wno-sign-compare", "-fno-strict-aliasing", "-fno-strict-overflow",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
                    "-I", str(ROOT / "tests"), "-xc", "-", "-o", exe],
                   input=unit, text=True, check=True)
    if vectors is not None:
        trace = out / "oob-traces.jsonl"
        subprocess.run([exe], check=True, env=dict(os.environ,
                       NAND_OOB_VECTORS=str(vectors), NAND_OOB_TRACE=str(trace)))
        raw = trace.read_bytes()
        return dict(v11=True, passed=True, functions=functions, nand_base_sha256=NAND_BASE,
                    driver_sha256=hashlib.sha256(driver.encode()).hexdigest(),
                    fixture_sha256=hashlib.sha256(unit.encode() + (ROOT / "tests/fixtures.h").read_bytes()).hexdigest(),
                    oob_traces_sha256=hashlib.sha256(raw).hexdigest(),
                    oob_vectors_sha256=hashlib.sha256(vectors.read_bytes()).hexdigest(),
                    oob_trace_count=len(raw.splitlines()))
    elif expected_failure is None:
        subprocess.run([exe], check=True)
    else:
        failure = subprocess.run([exe], cwd=out, capture_output=True, text=True)
        if failure.returncode != -signal.SIGABRT or expected_failure not in failure.stderr:
            raise RuntimeError(f"did not detect {expected_failure!r}: {failure.stderr}")


def mutations(driver, cc, out, vendor):
    # A compiler failure, sanitizer error or unrelated assertion is not
    # evidence that the relevant behavioural regression was detected.
    controls = [
        ("hw_ecc_read_error", "if (!step) {", "if (false) {", "ret == (int)corrected"),
        ("hw_ecc_read_error", "sunxi_get_oob_offset(sunxi_nand, ecc, step)",
         "sunxi_get_oob_offset(sunxi_nand, ecc, 0)", "r.max_bitflips == 3U + !!step"),
        ("hw_ecc_read_error", "if (ret >= 0)\n\t\tread->has_erased_chunk = true;",
         "read->has_erased_chunk = ret >= 0;", "r.has_erased_chunk == erased"),
        ("hw_ecc_read_error", "if (ret >= 0)", "if (ret > 0)", "r.has_erased_chunk == erased"),
        ("oob_finish_read", "!read->has_erased_chunk", "read->has_erased_chunk",
         "plain_read_calls - plain == erased"),
        ("oob_read_batch", "if (!step)", "if (step)", "marker_commands == (want && pattern)"),
        ("oob_finish_read", "if (read->read_raw_bbm)", "if (read->oob_required)",
         "marker_commands == (want && pattern)"),
        ("hw_ecc_read_page_dma", " || nfc->recovery_failed", "", "flow.page_attempts == 1U + retry"),
        ("hw_ecc_read_subpage_dma", " || nfc->recovery_failed", "", "flow.page_attempts == 1U + retry"),
        ("hw_ecc_read_chunks_dma", "mtd->ecc_stats.corrected = corrected;", "(void)corrected;",
         "n->mtd.ecc_stats.corrected == old_corrected"),
        ("hw_ecc_read_chunks_dma", "mtd->ecc_stats.failed = failed;", "(void)failed;",
         "n->mtd.ecc_stats.failed == old_failed"),
        ("hw_ecc_read_chunks_dma", ".first_req_step = first_req_step", ".first_req_step = 0",
         "n->mtd.ecc_stats.corrected == old_corrected + corrected"),
        ("hw_ecc_read_batch_dma", "read->buf + first_step * ecc->size", "read->buf",
         "flow.mapped_buf == main_data + first * n->ecc.size"),
        ("hw_ecc_read_batch_dma", "if (first_step)\n\t\twritel(mtd->writesize +",
         "if (false)\n\t\twritel(mtd->writesize +", "== n->mtd.writesize + off"),
        ("hw_ecc_read_batch_dma", "if (nfc->use_mdma)", "if (!nfc->use_mdma)",
         "flow.pending_issues == (controller.use_mdma ? 0 : flow.dma_issued)"),
        ("hw_ecc_read_batch_dma", "ret = sunxi_nfc_wait_events(nfc, wait, false, 0);",
         "sunxi_nfc_hw_ecc_read_status(nand, &snapshot);\n\tret = sunxi_nfc_wait_events(nfc, wait, false, 0);",
         "!flow.active"),
        ("hw_ecc_read_chunks_pio", "step = first_step", "step = 0", "observed_steps == mask"),
        ("hw_ecc_read_chunks_pio", "step < end_step", "step + 1 < end_step",
         "observed_steps == mask"),
        ("reset_user_data_len", "int i = loop_step;", "int i = 0;", "!mmio_writes[reg / 4]"),
        ("hw_ecc_write_page_dma", "if (first_step)\n\t\t\t\treturn ret;",
         "if (false)\n\t\t\t\treturn ret;", "flow.begins == 1 && col == 0 && len == 0"),
        ("hw_ecc_write_page_dma", "sunxi_nfc_wait_cmd_fifo_empty(nfc);\n\t\tif (ret)\n\t\t\treturn ret;",
         "sunxi_nfc_wait_cmd_fifo_empty(nfc);\n\t\tif (ret)\n\t\t\tgoto pio_fallback;",
         "ret == expected_error"),
        ("hw_ecc_write_oob", "return nand->ecc.write_page(nand, buf, 1, page);",
         "return nand->ecc.write_page(nand, buf, 1, page) ?: nand_prog_page_end_op(nand);",
         "flow.confirms == 1"),
        ("hw_ecc_write_page", "return nand_prog_page_end_op(nand);",
         "nand_prog_page_end_op(nand); return 0;", "ret == expected_error"),
        ("hw_ecc_write_page", "if (oob_required || (nand->options & NAND_NEED_SCRAMBLING))",
         "if (oob_required)", "tail_writes == need_tail"),
        ("hw_ecc_write_chunk", "sunxi_nfc_user_data_sz(sunxi_nand, step)",
         "sunxi_nfc_user_data_sz(sunxi_nand, 0)", "subwr_issue"),
        ("hw_ecc_write_page_dma", "if (first_step)\n\t\t\twritel(mtd->writesize +",
         "if (false)\n\t\t\twritel(mtd->writesize +", "== n->mtd.writesize + offset"),
        ("hw_ecc_write_extra_oob", "*cur_off != offset + mtd->writesize", "*cur_off != offset",
         "!again && ret == 0 && flow.confirms == 1 && flow.begins == 1"),
        ("hw_ecc_write_page_dma", "return sunxi_nfc_hw_ecc_write_page(nand, buf, oob_required, page);",
         "sunxi_nfc_hw_ecc_write_page(nand, buf, oob_required, page); return 0;", "ret == expected_error"),
    ] if not vendor else [
        ("allwinner_oob_finish_read", "if (result->zero_steps ==", "if (false && result->zero_steps ==",
         "n.mtd.ecc_stats.failed == (i != 0)"),
    ]
    for name, before, after, oracle in controls:
        body = extract(driver, "sunxi_nfc_" + name)
        if body.count(before) != 1:
            raise ValueError("mutation does not match current " + name)
        test_driver(driver.replace(body, body.replace(before, after), 1), cc, out, oracle)
        print(f"Detected source mutation: {name}", flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc", default="gcc")
    p.add_argument("--lifecycle", action="store_true",
                   help="also exercise scan, publication and unwind on each tip with GCC/gcov")
    p.add_argument("--mutations", action="store_true",
                   help="require each deliberate regression to fail its specific assertion")
    p.add_argument("--ordinary", type=Path)
    p.add_argument("--firmware", type=Path)
    args = p.parse_args()
    if bool(args.ordinary) != bool(args.firmware):
        p.error("provide both candidate files or neither")
    with tempfile.TemporaryDirectory() as dest:
        tmp = Path(dest)
        ordinary, firmware = (args.ordinary, args.firmware) if args.ordinary else pinned_pair(tmp)
        nand_base = tmp / "nand_base.c"
        nand_base.write_text(pinned_nand_base())
        subprocess.run([sys.executable, ROOT / "check_ecc_read_policy.py", ordinary, firmware], check=True)
        for path in (ordinary, firmware):
            print(f"Source SHA-256: {hashlib.sha256(path.read_bytes()).hexdigest()}", flush=True)
            marker = [sys.executable, ROOT / "run_marker_admission.py", path,
                      "--core", nand_base, "--cc", args.cc]
            subprocess.run(marker, check=True)
            test_driver(path.read_text(), args.cc, tmp)
            if args.mutations:
                mutations(path.read_text(), args.cc, tmp, path == firmware)
                if path == ordinary:
                    subprocess.run([*marker, "--omit-ordinary-guard"], check=True)
            if args.lifecycle:
                subprocess.run([sys.executable, ROOT / "run_lifecycle.py", "--candidate-source", path,
                                "--candidate-nand-base", nand_base,
                                "--expect-available-children", "--expect-deferred-publication",
                                "--probe-traces"], check=True)


if __name__ == "__main__":
    main()
