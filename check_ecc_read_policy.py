#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the ECC policy proof's structural boundaries against supplied sunxi_nand.c files."""
import argparse
import hashlib
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


def ordinary(source, snapshot):
    # The first refactor reads the pattern ID directly. Later patches
    # cache the same register bits and propagate transport errors.
    field = "snapshot->" if snapshot else ""
    batch = body(source, PREFIX + "oob_read_batch")
    ordered(batch, f"if ({field}status & NFC_ECC_ERR(slot))", "continue;",
            f"if ({field}pattern_found & BIT(slot))", "pattern_id & BIT(slot) ? 0xff : 0",
            "if (!read->dma || randomized)\n\t\t\t\tmemset(data, pattern, ecc->size);",
            "if (pattern == 0xff)", "read->has_erased_chunk = true;", "continue;",
            "bitflips = sunxi_nfc_hw_ecc_read_count(", "sunxi_nfc_hw_ecc_update_stats(",
            f"if (!({field}status & NFC_ECC_ERR(slot)))", "sunxi_nfc_hw_ecc_read_error(",
            "read->has_erased_chunk = true;")
    finish = body(source, PREFIX + "oob_finish_read")
    ordered(finish, "if (read->oob_required)", "sunxi_nfc_hw_ecc_read_extra_oob(",
            "!read->has_erased_chunk", "return read->max_bitflips;")
    ordered(source, "#define NFC_REG_ECC_ERR_CNT(nfc, x)\t((nfc->caps->reg_ecc_err_cnt + (x)) & ~0x3)",
            "#define NFC_ECC_ERR_CNT(b, x)\t(((x) >> (((b) % 4) * 8)) & 0xff)")
    if snapshot:
        ordered(body(source, PREFIX + "hw_ecc_read_status"), "snapshot->count_reg = ~0U;")
        ordered(body(source, PREFIX + "hw_ecc_read_count"), "reg = NFC_REG_ECC_ERR_CNT(nfc, slot);",
                "if (snapshot->count_reg != reg)", "snapshot->count = readl(nfc->regs + reg);",
                "snapshot->count_reg = reg;", "return NFC_ECC_ERR_CNT(slot, snapshot->count);")


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
    # All three firmware decision bodies predate this split. Their exact
    # bytes are exercised by the published reference C fixture; the removed
    # unused finish parameter isn't part of its body.
    for suffix in ("record_status", "read_batch", "finish_read"):
        name = VENDOR + suffix
        if body(firmware, name) != body(reference, name):
            raise ValueError(f"{name}: no longer identical to the tested reference body")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("ordinary", type=Path, help="bug-fix series tip, after snapshot caching")
    p.add_argument("firmware", type=Path, help="same candidate plus the proposed format")
    p.add_argument("--first-refactor", type=Path, help="ordinary policy before snapshot caching")
    args = p.parse_args()
    plain = args.ordinary.read_text()
    firmware = args.firmware.read_text()
    reference = (HERE / "reference/sunxi_nand.c").read_text()
    ordinary(plain, snapshot=True)
    ordinary(firmware, snapshot=True)
    compare(plain, firmware, reference)
    if args.first_refactor:
        ordinary(args.first_refactor.read_text(), snapshot=False)
    for label, path in vars(args).items():
        if path is not None:
            print(f"{label}: {hashlib.sha256(path.read_bytes()).hexdigest()}")
    print("PASS: ordinary control order, two early dispatches, and three firmware bodies")


if __name__ == "__main__":
    main()
