#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute ONFI/extended-ID decoding into actual sunxi attach/layout selection."""
import re
import sys
from boundary_common import HERE, sources, bodies, check
from driver_boundary import generate

FUNCTIONS = {
    "nand_base.c": ["sanitize_string", "nand_id_has_period", "nand_id_len",
                    "nand_get_bits_per_cell", "nand_decode_ext_id", "nand_decode_id"],
    "nand_onfi.c": ["onfi_crc16", "nand_flash_detect_ext_param_page",
                    "nand_bit_wise_majority", "nand_onfi_detect"],
}

if __name__ == "__main__":
    if sys.byteorder != "little":
        raise SystemExit("ONFI host fixtures currently require a little-endian host")
    src=sources()
    code,proto,hashes=bodies(src,FUNCTIONS)
    header=re.sub(r"^#include.*\n", "", src["onfi.h"],flags=re.M)
    fixture=(HERE/"tests/discovery_boundary.h").read_text().replace("/* @onfi@ */",header)
    driver,driver_hashes=generate(src)
    check("discovery-boundary", {"core.c": fixture + proto + "\n" + code +
        '\n#line 1 "discovery_boundary.c"\n'+(HERE/"tests/discovery_boundary.c").read_text(),
        "driver.c":driver},dict(hashes,**driver_hashes),
        flags=["-Wno-pointer-sign"],coverage=(src,FUNCTIONS),limits=[
            "Adapter passes decoded values across separate kernel-structure fixtures",
            "Vendor ID overrides and real command transport remain separate checks",
            "Valid bounded geometry; not arbitrary adversarial ONFI integer fuzzing"])
