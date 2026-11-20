#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute NAND identification and sunxi ECC attach against a supplied format candidate."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from run_c import HERE, extract_function
from run_core import extract


def declaration(source, start):
    return re.search("^" + re.escape(start) + r".*?^};", source, re.M | re.S)[0]


def hamming_core(core):
    """Execute the core's on-host -> Hamming dispatch with its actual layout/geometry functions."""
    scan = extract(core, "nand_scan_tail")
    start = scan.index("switch (ecc->engine_type) {\n\tcase NAND_ECC_ENGINE_TYPE_ON_HOST:")
    dispatch = scan[start:scan.index("case NAND_ECC_ENGINE_TYPE_ON_DIE:", start)]
    soft = extract(core, "nand_set_ecc_soft_ops").split("case NAND_ECC_ALGO_HAMMING:", 1)[1]
    assert "rawnand_sw_hamming_init(chip)" in soft.split("case NAND_ECC_ALGO_BCH:", 1)[0]
    return "\n".join([(HERE / "tests/marker_fallback_core.c").read_text(),
                      (HERE / "reference/sunxi-v11/small-page-ecc.c").read_text(),
                      extract(core, "rawnand_sw_hamming_init"), """
struct hamming_geometry hamming_geometry(int on_host, int controller_layout)
{
    struct mtd_info media = {512, 16, controller_layout ? CONTROLLER : 0};
    struct nand_chip chip_data = {.base.mtd = &media};
    struct nand_chip *chip = &chip_data;
    struct mtd_info *mtd = &media;
    struct nand_ecc_ctrl *ecc = &chip->ecc;
    int ret;
    active = &chip->base;
    ecc->engine_type = on_host ? NAND_ECC_ENGINE_TYPE_ON_HOST : NAND_ECC_ENGINE_TYPE_SOFT;
    ecc->algo = NAND_ECC_ALGO_HAMMING;
    ecc->size = 1024; ecc->strength = 16;
""", dispatch, """
    }
    assert(ecc->engine_type == NAND_ECC_ENGINE_TYPE_SOFT);
    struct hamming_geometry g = {ecc->size, ecc->bytes, ecc->steps, mtd->ooblayout == CONTROLLER, 0};
    if (!g.controller_layout) {
        assert(mtd->ooblayout == SMALL_PAGE);
        for (int i = 0; i < 2; i++) {
            struct mtd_oob_region p;
            assert(!nand_ooblayout_ecc_sp(mtd, i, &p));
            g.core_marker_in_parity |= p.offset <= 5 && 5 < p.offset + p.length;
        }
    }
    struct nand_ecc_sw_hamming_conf *h = active->ecc.ctx.priv;
    free(h->calc_buf); free(h->code_buf); free(h);
    active = NULL;
    return g;
err_nand_manuf_cleanup:
    abort();
}"""])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("driver", type=Path)
    p.add_argument("--core", type=Path, default=HERE / "reference/nand_base.c")
    p.add_argument("--header", type=Path, default=HERE / "reference/rawnand.h")
    p.add_argument("--cc", default="cc")
    p.add_argument("--expect-unguarded", action="store_true",
                   help="historical firmware candidate without either marker guard")
    p.add_argument("--expect-plain-unguarded", action="store_true",
                   help="historical ordinary layout allowed markers in parity")
    p.add_argument("--omit-ordinary-guard", action="store_true",
                   help="negative control: bypass just the ordinary marker guard")
    args = p.parse_args()
    src, header = args.driver.read_text(), args.header.read_text()
    if args.omit_ordinary_guard:
        guard = "if (nand->badblockpos >= SUNXI_NFC_BBM_SIZE)"
        if src.count(guard) != 1:
            p.error("expected exactly one ordinary marker guard")
        src = src.replace(guard, "if (false)")
    vendor = "sunxi_nfc_allwinner_oob_attach_chip(" in src
    if args.expect_unguarded and not vendor:
        p.error("use --expect-plain-unguarded for an ordinary-only source")
    parts = ['#include "fixtures.h"', '#define round_down(x, y) ((x) & ~((y) - 1))']
    parts += ['#include "marker_fallback.h"', 'static bool controller_layout;',
              '#undef mtd_set_ooblayout', 'static int sunxi_nand_ooblayout_ops;',
              '#define mtd_set_ooblayout(m, ops) (controller_layout = (ops) == &sunxi_nand_ooblayout_ops)']
    marker_size = re.search(r"^#define SUNXI_NFC_BBM_SIZE\s+\d+", src, re.M)
    if marker_size:
        parts.append(marker_size[0])
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
    ordinary = ("sunxi_nfc_user_data_before", "sunxi_nfc_user_data_sz", "sunxi_get_ecc_offset",
                "sunxi_get_oob_offset", "sunxi_nand_ooblayout_ecc", "sunxi_nand_ooblayout_free",
                "sunxi_nfc_fixed_min_user_data", "sunxi_nfc_variable_min_user_data",
                "sunxi_nfc_variable_init_user_data")
    extra = ("sunxi_nfc_allwinner_prefix_size", "sunxi_nfc_allwinner_init_prefix",
             "sunxi_nfc_allwinner_oob_read_subpage") if vendor else ()
    for name in ordinary + extra:
        parts.append(extract_function(src, name))
    for layout in (("fixed", "variable", "allwinner_prefix") if vendor else ("fixed", "variable")):
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
    for name in (("sunxi_nand_hw_ecc_ctrl_init",) +
                 (("sunxi_nfc_allwinner_oob_attach_chip",) if vendor else ()) +
                 ("sunxi_nand_attach_chip",)):
        parts.append(extract_function(src, name))
    parts.append((HERE / "tests/marker_admission.c").read_text())
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "marker"
        # Compile the real core functions separately: layout mocks for the
        # controller do not participate in the core's layout selection.
        core_obj = Path(tmp) / "hamming.o"
        flags = [args.cc, "-std=gnu11", "-g", "-O1", "-Werror", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-I", str(HERE / "tests")]
        subprocess.run([*flags, "-xc", "-", "-c", "-o", core_obj], check=True, text=True,
                       input=hamming_core(args.core.read_text()))
        subprocess.run([*flags, "-no-pie", core_obj, f"-DUNGUARDED={int(args.expect_unguarded)}",
                        f"-DPLAIN_UNGUARDED={int(args.expect_plain_unguarded or args.expect_unguarded or args.omit_ordinary_guard)}",
                        f"-DHAS_FIRMWARE={int(vendor)}",
                        "-o", str(exe), "-xc", "-"], input="\n".join(parts), text=True, check=True)
        subprocess.run([exe], check=True)


if __name__ == "__main__":
    main()
