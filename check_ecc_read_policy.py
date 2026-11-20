#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the ECC policy proof's structural boundaries against supplied sunxi_nand.c files."""
import argparse
import hashlib
import re
from pathlib import Path

from run_c import extract_function

HERE = Path(__file__).resolve().parent
PREFIX = "sunxi_nfc_"
VENDOR = PREFIX + "allwinner_oob_"


def body(source, name):
    function = extract_function(source, name)
    _, delimiter, implementation = function.partition("\n{\n")
    if not delimiter or not implementation:
        raise ValueError(f"cannot locate the body of {name}")
    return implementation


def ordered(code, *fragments):
    """Deliberately fail on source drift rather than accepting a changed implementation."""
    position = 0
    for fragment in fragments:
        index = code.find(fragment, position)
        if index == -1:
            raise ValueError(f"missing or out-of-order fragment: {fragment}")
        position = index + len(fragment)


def ordinary(source, snapshot, window=False):
    # The first refactor reads the pattern ID directly. Later patches
    # cache the same register bits and propagate transport errors.
    field = "snapshot->" if snapshot else ""
    batch = body(source, PREFIX + "oob_read_batch")
    raw_guard = "step < read->first_req_step || " if window else ""
    ordered(batch, f"if ({field}status & NFC_ECC_ERR(slot))", "continue;",
            f"if ({field}pattern_found & BIT(slot))", "pattern_id & BIT(slot) ? 0xff : 0",
            "if (!read->dma || randomized)\n\t\t\t\tmemset(data, pattern, ecc->size);",
            "if (pattern == 0xff)", "read->has_erased_chunk = true;", "continue;",
            "bitflips = sunxi_nfc_hw_ecc_read_count(", "sunxi_nfc_hw_ecc_update_stats(",
            f"if ({raw_guard}!({field}status & NFC_ECC_ERR(slot)))", "sunxi_nfc_hw_ecc_read_error(",
            "read->has_erased_chunk = true;")
    finish = body(source, PREFIX + "oob_finish_read")
    ordered(finish, "if (read->oob_required)", "sunxi_nfc_hw_ecc_read_extra_oob(",
            "!read->has_erased_chunk", "return read->max_bitflips;")
    ordered(source, "#define NFC_REG_ECC_ERR_CNT(nfc, x)\t((nfc->caps->reg_ecc_err_cnt + (x)) & ~0x3)",
            "#define NFC_ECC_ERR_CNT(b, x)\t(((x) >> (((b) % 4) * 8)) & 0xff)")
    if snapshot:
        status = body(source, PREFIX + "hw_ecc_read_status")
        ordered(status, "snapshot->count_reg = ~0U;")
        if window:
            helper = PREFIX + "hw_ecc_read_pattern_id"
            if "pattern_id_valid" in source or helper in status:
                raise ValueError("expected the caller to select the pattern register read")
            ordered(batch, "if (snapshot->pattern_found & ~snapshot->status)",
                    helper + "(nand, snapshot);", "for (slot")
            if batch.count(helper) != 1 or body(source, helper).count("readl(") != 1:
                raise ValueError("expected a single pattern ID access at the selected site")
        ordered(body(source, PREFIX + "hw_ecc_read_count"), "reg = NFC_REG_ECC_ERR_CNT(nfc, slot);",
                "if (snapshot->count_reg != reg)", "snapshot->count = readl(nfc->regs + reg);",
                "snapshot->count_reg = reg;", "return NFC_ECC_ERR_CNT(slot, snapshot->count);")


def subpage_range(source, fixed):
    """Check both ordinary passes and their actual DMA/PIO callers."""
    def code(name):
        return " ".join(body(source, PREFIX + name).split())

    chunks = code("hw_ecc_read_chunks_dma")
    sub = code("hw_ecc_read_subpage_dma")
    batch = code("oob_read_batch")
    shared_pio = PREFIX + "hw_ecc_read_chunks_pio(" in source
    if fixed:
        ordered(sub, "first_step = data_offs / nand->ecc.size;",
                "end_step = DIV_ROUND_UP(data_offs + readlen, nand->ecc.size);",
                "sunxi_nfc_hw_ecc_read_chunks_dma(nand, buf, false, page, first_step, end_step)")
        ordered(chunks, ".first_req_step = first_req_step,",
                "for (first_step = 0; first_step < end_step; first_step += batch_steps)",
                "sunxi_nfc_dma_batch_steps(nand, first_step, end_step);",
                "sunxi_nfc_hw_ecc_read_batch_dma(nand, &read, first_step, batch_steps)")
        ordered(code("hw_ecc_read_page_dma"),
                "sunxi_nfc_hw_ecc_read_chunks_dma(nand, buf, oob_required, page, 0, nand->ecc.steps)")
        raw_check = ("sunxi_nfc_hw_ecc_read_error(nand, read, step);",) if shared_pio else (
            "data_off = step * ecc->size;", "sunxi_nfc_hw_ecc_read_error(")
        ordered(batch, "slot++, reg_index += user_data_sz / 4)",
                "int step = first_step + slot;",
                "user_data_sz = sunxi_nfc_user_data_sz(sunxi_nand, step);",
                "if (step < read->first_req_step) continue;",
                "if (snapshot->status & NFC_ECC_ERR(slot))",
                "sunxi_nfc_hw_ecc_read_count(nand, snapshot, slot)",
                "int step = first_step + slot;",
                "if (step < read->first_req_step || !(snapshot->status & NFC_ECC_ERR(slot))) continue;",
                *raw_check)
    else:
        if "first_req_step" in chunks or "first_req_step" in batch:
            raise ValueError("expected the pre-fix prefix-accounting code")
        ordered(sub, "nchunks = DIV_ROUND_UP(data_offs + readlen, nand->ecc.size)",
                "sunxi_nfc_hw_ecc_read_chunks_dma(nand, buf, false, page, nchunks)")
        ordered(chunks, "for (first_step = 0; first_step < nchunks; first_step += batch_steps)")
    if shared_pio:
        ordered(code("hw_ecc_read_page"),
                "sunxi_nfc_hw_ecc_read_chunks_pio(nand, buf, oob_required, page, 0, nand->ecc.steps)")
        ordered(code("hw_ecc_read_subpage"), "first_step = data_offs / nand->ecc.size;",
                "end_step = DIV_ROUND_UP(data_offs + readlen, nand->ecc.size);",
                "sunxi_nfc_hw_ecc_read_chunks_pio(nand, buf, false, page, first_step, end_step)")
        ordered(code("hw_ecc_read_chunks_pio"), "for (step = first_step; step < end_step; step++)",
                "sunxi_nfc_hw_ecc_read_chunk(nand, &read, step, &cur_off)")
    else:
        ordered(code("hw_ecc_read_subpage"), "for (i = data_offs / ecc->size;",
                "i < DIV_ROUND_UP(data_offs + readlen, ecc->size); i++)",
                "sunxi_nfc_hw_ecc_read_chunk(nand, &read, i, &cur_off)")


def compare(plain, firmware, reference):
    # Compare whole ordinary function bodies, not just the two function
    # signatures. Require each dispatch *before* either ordinary path acts.
    for suffix, condition, arguments, first in (
        ("read_batch", "sunxi_nand->allwinner_oob", "nand, read, snapshot, first_step, nsteps",
         "for (slot"),
        ("finish_read", "to_sunxi_nand(nand)->allwinner_oob", "nand, read",
         "if (read->oob_required)"),
    ):
        name = PREFIX + "oob_" + suffix
        before, after = body(plain, name), body(firmware, name)
        dispatch = f"if ({condition})\n\t\treturn {VENDOR}{suffix}({arguments});"
        ordered(after, dispatch, first)
        if after.count(dispatch) != 1 or after.replace("\t" + dispatch + "\n\n", "") != before:
            raise ValueError(f"{name}: firmware extension changed the ordinary body")
    # Compare the bodies exercised by the reference C fixture. Standalone
    # comments may be reworded, but retain all executable lines verbatim.
    def implementation(source, name):
        return re.sub(r"(?m)^[ \t]*/\*(?:[^*]|\*(?!/))*\*/[ \t]*\n", "",
                      body(source, name))

    for suffix in ("record_status", "read_batch", "finish_read"):
        name = VENDOR + suffix
        if implementation(firmware, name) != implementation(reference, name):
            raise ValueError(f"{name}: no longer identical to the tested reference body")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("ordinary", type=Path, help="bug-fix series tip, after snapshot caching")
    p.add_argument("firmware", type=Path, help="same candidate plus the proposed format")
    p.add_argument("--first-refactor", type=Path, help="ordinary policy before snapshot caching")
    p.add_argument("--expect-prefix-bug", action="store_true",
                   help="compare the known pre-fix tips instead of requiring request-range guards")
    args = p.parse_args()
    plain = args.ordinary.read_text()
    firmware = args.firmware.read_text()
    reference = (HERE / "reference/sunxi_nand.c").read_text()
    for src in (plain, firmware):
        ordinary(src, snapshot=True, window=not args.expect_prefix_bug)
        subpage_range(src, fixed=not args.expect_prefix_bug)
    ordered(firmware, "nand->ecc.read_subpage = sunxi_nfc_allwinner_oob_read_subpage;")
    if not args.expect_prefix_bug:
        ordered(body(firmware, PREFIX + "oob_read_batch"), "if (sunxi_nand->allwinner_oob)",
                "return sunxi_nfc_allwinner_oob_read_batch(", "if (snapshot->pattern_found & ~snapshot->status)")
    if body(firmware, VENDOR + "read_subpage") != body(reference, VENDOR + "read_subpage"):
        raise ValueError("firmware subpage no longer uses the full-page read")
    compare(plain, firmware, reference)
    if args.first_refactor:
        ordinary(args.first_refactor.read_text(), snapshot=False)
    for label, path in vars(args).items():
        if isinstance(path, Path):
            print(f"{label}: {hashlib.sha256(path.read_bytes()).hexdigest()}")
    scope = "pre-fix prefix" if args.expect_prefix_bug else "requested steps"
    print(f"PASS: ordinary control order ({scope}), two early dispatches, and full-page firmware")


if __name__ == "__main__":
    main()
