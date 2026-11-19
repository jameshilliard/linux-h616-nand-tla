#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute NAND identification and sunxi ECC attach against a supplied format candidate."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from run_c import HERE, extract_function


def declaration(source, start):
    return re.search("^" + re.escape(start) + r".*?^};", source, re.M | re.S)[0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("driver", type=Path)
    p.add_argument("--core", type=Path, default=HERE / "reference/nand_base.c")
    p.add_argument("--header", type=Path, default=HERE / "reference/rawnand.h")
    p.add_argument("--cc", default="cc")
    p.add_argument("--expect-unguarded", action="store_true")
    args = p.parse_args()
    src, header = args.driver.read_text(), args.header.read_text()
    parts = ['#include "fixtures.h"', '#define round_down(x, y) ((x) & ~((y) - 1))']
    for symbol in ("NAND_BUSWIDTH_16", "NAND_BBM_POS_LARGE", "NAND_BBM_POS_SMALL",
                   "NAND_NEED_SCRAMBLING", "NAND_NO_SUBPAGE_WRITE", "NAND_SUBPAGE_READ",
                   "NAND_USES_DMA"):
        parts.extend(["#undef " + symbol, re.search(r"^#define " + symbol + r"\s+[^\n]+",
                                                    header, re.M)[0]])
    # Attach installs but never executes page-transfer callbacks. Their bodies
    # (including DMA/MMIO) are out of scope; a call through a stub would fail.
    for suffix in ("read_oob", "write_oob", "read_page", "read_page_dma",
                   "write_page", "write_page_dma", "read_subpage", "read_subpage_dma",
                   "write_subpage"):
        parts.append(f"#define sunxi_nfc_hw_ecc_{suffix} NULL")
    parts.extend([extract_function(args.core.read_text(), "nand_decode_bbm_options"),
                  declaration(src, "struct sunxi_nfc_user_data_layout {")])
    for name in ("sunxi_nfc_user_data_before", "sunxi_get_ecc_offset", "sunxi_get_oob_offset",
                 "sunxi_nfc_fixed_min_user_data",
                 "sunxi_nfc_variable_min_user_data", "sunxi_nfc_variable_init_user_data",
                 "sunxi_nfc_allwinner_prefix_size", "sunxi_nfc_allwinner_init_prefix",
                 "sunxi_nfc_allwinner_oob_read_subpage"):
        parts.append(extract_function(src, name))
    for layout in ("fixed", "variable", "allwinner_prefix"):
        parts.append(declaration(src, "static const struct sunxi_nfc_user_data_layout sunxi_nfc_"
                                 + layout + "_layout = {"))
    for soc in ("a10", "h6"):
        parts.append(declaration(src, "static const u8 sunxi_ecc_strengths_" + soc + "[] = {"))
    for soc in ("a10", "a23", "h616"):
        init = declaration(src, f"static const struct sunxi_nfc_caps sunxi_nfc_{soc}_caps = {{")
        fields = re.findall(r"^\s*(\.(?:layout|has_ecc_block_512|reg_user_data_len|ecc_mode_mask|"
                            r"ecc_strengths|nstrengths|max_user_data_len|max_ecc_steps) = [^\n]+)",
                            init, re.M)
        parts.append(f"static struct caps {soc}_caps = {{\n" + "\n".join(fields) + "\n};")
    parts.insert(1, re.search(r"^#define NFC_REG_H6_USER_DATA_LEN\s+0x\w+", src, re.M)[0])
    for name in ("sunxi_nand_hw_ecc_ctrl_init", "sunxi_nfc_allwinner_oob_attach_chip",
                 "sunxi_nand_attach_chip"):
        parts.append(extract_function(src, name))
    parts.append((HERE / "tests/marker_admission.c").read_text())
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "marker"
        subprocess.run([args.cc, "-std=gnu11", "-g", "-O1", "-Werror", "-no-pie",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(HERE / "tests"), f"-DUNGUARDED={int(args.expect_unguarded)}",
                        "-o", str(exe), "-xc", "-"], input="\n".join(parts), text=True, check=True)
        subprocess.run([exe], check=True)


if __name__ == "__main__":
    main()
