#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the raw-NAND lifetime conversion inventory, not semantic correctness.

Every scan provider must map to an explicitly reviewed conversion. This catches
omitted/new drivers; the presence of ownership calls is not proof that their
ordering is correct. Host traces, TLC and real-kernel tests check that contract.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

DRIVERS = """
ams-delta.c arasan-nand-controller.c atmel/nand-controller.c au1550nd.c
bcm47xxnflash/main.c brcmnand/brcmnand.c cadence-nand-controller.c cafe_nand.c
cs553x_nand.c davinci_nand.c denali.c diskonchip.c fsl_elbc_nand.c fsl_ifc_nand.c
fsl_upm.c fsmc_nand.c gpio.c gpmi-nand/gpmi-nand.c hisi504_nand.c
ingenic/ingenic_nand_drv.c intel-nand-controller.c loongson-nand-controller.c
lpc32xx_mlc.c lpc32xx_slc.c marvell_nand.c meson_nand.c mpc5121_nfc.c mtk_nand.c
mxc_nand.c mxic_nand.c nandsim.c ndfc.c nuvoton-ma35d1-nand-controller.c omap2.c
orion_nand.c pasemi_nand.c pl35x-nand-controller.c plat_nand.c qcom_nandc.c r852.c
renesas-nand-controller.c rockchip-nand-controller.c sh_flctl.c sharpsl.c
socrates_nand.c stm32_fmc2_nand.c sunxi_nand.c technologic-nand-controller.c
tegra_nand.c txx9ndfmc.c vf610_nfc.c xway_nand.c
""".split()

# These wrappers scan the chip; their callers own publication and lifetime.
SCAN_OWNER = {"sm_common.c": "r852.c", "bcm47xxnflash/ops_bcm4706.c": "bcm47xxnflash/main.c"}
EXTRA_FILES = {"denali.c": ["denali_dt.c", "denali_pci.c"], "r852.c": ["sm_common.c"]}
SPECIAL = {
    "omap2.c": "Static shared controller lock; per-binding storage has a separate kref.",
    "fsl_elbc_nand.c": "Shared parent controller kref; active-bank count separate from storage lifetime.",
    "fsl_ifc_nand.c": "Shared parent controller kref; active-bank count separate from storage lifetime.",
    "nandsim.c": "Module-only binding; final chip release owns the outer allocation.",
    "cs553x_nand.c": "Module-only binding; final chip release owns the outer allocation.",
    "diskonchip.c": "Module-only binding; final chip release owns the outer allocation.",
    "r852.c": "New chip per inserted card; PCI storage also survives IRQ/workqueue teardown.",
    "ndfc.c": "New controller per binding instead of reinitializing static storage.",
    "cafe_nand.c": "DMA detach runs while hardware resources are live; final detach is idempotent.",
    "cadence-nand-controller.c": "Shared buffers allocated before publication; track registered chips on unwind.",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-tree", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.kernel_tree / "drivers/mtd/nand/raw"
    sources = {str(path.relative_to(root)): path.read_text() for path in root.rglob("*.c")}
    scan = re.compile(r"\bnand_scan(?:_with_ids)?\s*\(")
    providers = {name for name, source in sources.items() if scan.search(source)} - {"nand_base.c"}
    owners = {SCAN_OWNER.get(name, name) for name in providers}
    expected = set(DRIVERS)
    errors = []
    if owners != expected:
        errors.append(f"scan inventory changed: new={sorted(owners - expected)}, missing={sorted(expected - owners)}")
    records = []
    for name in sorted(expected):
        files = [name] + EXTRA_FILES.get(name, [])
        source = "\n".join(sources.get(file, "") for file in files)
        missing = [call for call in ("nand_set_release", "nand_disconnect", "mtd_put_master")
                   if not re.search(r"\b" + call + r"\s*\(", source)]
        if missing:
            errors.append(f"{name}: missing ownership hooks {missing}")
        if name not in {"omap2.c", "fsl_elbc_nand.c", "fsl_ifc_nand.c", "nandsim.c",
                        "cs553x_nand.c", "diskonchip.c"} and "devm_nand_controller_init(" not in source:
            errors.append(f"{name}: missing managed controller initialization")
        records.append(dict(driver=name, files=files, note=SPECIAL.get(name,
            "Managed controller storage and retained chip metadata."),
            sha256={file: hashlib.sha256(sources[file].encode()).hexdigest() for file in files}))
    revision = subprocess.check_output(["git", "-C", str(args.kernel_tree), "rev-parse", "HEAD"], text=True).strip()
    report = dict(revision=revision, passed=not errors, errors=errors, drivers=records,
                  scope="All raw-NAND scan providers; not SPI NAND, OneNAND or other MTD technologies.")
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    for error in errors:
        print("FAIL:", error)
    print(f"{len(records)} raw-NAND driver families: structural inventory {'PASS' if not errors else 'FAIL'}")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
