# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact pre-series Linux/BSP bodies for the format compatibility harness."""
import hashlib
import json
import re
from run_core import extract

OLD = ["sunxi_nfc_randomizer_state", "sunxi_nfc_user_data_sz",
       "sunxi_get_oob_offset", "sunxi_get_ecc_offset",
       "sunxi_nand_ooblayout_ecc", "sunxi_nand_ooblayout_free"]
BSP = ["_cal_random_seed", "ndfc_get_spare_data", "ndfc_set_spare_data",
       "ndfc_set_user_data_len", "ndfc_set_user_data_len_cfg",
       "ndfc_set_user_data_len_cfg_4bytesper1k"]


def generate(root):
    directory = root / "reference/compat"
    pin = json.loads((directory / "source.json").read_text())
    sources = {}
    for name, digest in pin["files"].items():
        data = (directory / name).read_bytes()
        assert hashlib.sha256(data).hexdigest() == digest, name
        sources[name] = data.decode()
    result, hashes = [], {}
    old = sources["sunxi_nand_base.c"]
    # Namespace via preprocessor, leaving each extracted function body intact.
    result.extend(f"#define {name} old_{name}" for name in OLD)
    for name in ("sunxi_nfc_randomizer_page_seeds", "sunxi_nfc_randomizer_ecc512_seeds",
                 "sunxi_nfc_randomizer_ecc1024_seeds"):
        result.append(f"#define {name} old_{name}")
        result.append(re.search(r"^static const u16 " + name + r"\[\] = \{.*?\n};",old,re.M|re.S)[0])
    for name in OLD:
        body = extract(old,name)
        result.append(body)
        hashes["old:" + name] = hashlib.sha256(body.encode()).hexdigest()
    result.extend(f"#undef {name}" for name in OLD)
    for name in ("sunxi_nfc_randomizer_page_seeds", "sunxi_nfc_randomizer_ecc512_seeds",
                 "sunxi_nfc_randomizer_ecc1024_seeds"):
        result.append(f"#undef {name}")
    result.append((root / "tests/compat.h").read_text())
    bsp = sources["ndfc_base.c"]
    result.extend(("#define readl bsp_readl", "#define writel bsp_writel"))
    result.append(re.search(r"^const u32 random_seed\[128\] = \{.*?\n};",bsp,re.M|re.S)[0])
    for name in BSP:
        body = extract(bsp,name)
        result.append(body)
        hashes["bsp:" + name] = hashlib.sha256(body.encode()).hexdigest()
    result.extend(("#undef readl", "#undef writel"))
    # Use the exact normal-page spare-capacity expression, not boot0's format.
    start = sources["rawnand_chip.c"].index("\tnci->ecc_sector = 1;")
    end = sources["rawnand_chip.c"].index("\n\n",start)
    snippet = sources["rawnand_chip.c"][start:end]
    result.append("static void bsp_spare_capacity(struct nand_chip_info *nci)\n{\n"+snippet+"\n}")
    hashes["bsp:spare-capacity-block"] = hashlib.sha256(snippet.encode()).hexdigest()
    return "\n".join(result), hashes
