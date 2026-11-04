#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""NAND-core requests and BBT operations through real sunxi page callbacks."""
import re
from boundary_common import HERE, sources, bodies, check
from driver_boundary import generate
from run_core import FUNCTIONS


def units():
    src=sources()
    header=(HERE/"tests/core.h").read_text().replace("/* @bbm@ */",src["bbm.h"])
    defines=[]
    for name in ("rawnand.h","nand_bbt.c"):
        for m in re.finditer(r"^#define (?:NAND_|BBT_)[^\n\\]*(?:\\\n[^\n\\]*)*",src[name],re.M):
            if name=="nand_bbt.c" or re.match(r"#define NAND_(?:BUSWIDTH_16|NEED_READRDY|NO_SUBPAGE_WRITE|USES_DMA|BBM_FIRSTPAGE|BBM_SECONDPAGE|BBM_LASTPAGE|NO_BBM_QUIRK)\s",m[0]):
                defines.append(m[0])
    code,proto,hashes=bodies(src,FUNCTIONS)
    driver,dh=generate(src,persistent=True)
    return {"check.c":"\n".join(defines)+"\n"+header+proto+"\n"+code+
            '\n#line 1 "composed_core.c"\n'+(HERE/"tests/composed_core.c").read_text(),
            "driver.c":driver},dict(hashes,**dh),src


if __name__=="__main__":
    unit,hashes,src=units()
    coverage_names = {}
    for key in hashes:
        filename, name = key.split(":", 1)
        coverage_names.setdefault(filename, []).append(name)
    # Report unused extracted helpers as well; these request scenarios are
    # deliberately narrower than the dedicated driver and core matrices.
    check("composed",unit,hashes,coverage=(src,coverage_names),
        require_all_called=False,limits=[
        "Separate-TU value adapters, not kernel struct ABI or locking execution",
        "Actual core/BBT/page/ECC/OOB/randomizer bodies; command transport is a fixture",
        "Raw storage adapter, opaque parity, simulated ECC status and DMA memory transfer",
        "No real bus coherency, analog BCH correction, or device-core concurrency"])
