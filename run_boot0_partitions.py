#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the single-map sunxi partition registration prototype without flash I/O."""
import argparse
from pathlib import Path

from boundary_common import bodies, check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, help="kernel tree (default: pinned extracted code)")
    parser.add_argument("--save-extracted", type=Path)
    args = parser.parse_args()
    source = ((args.kernel / "drivers/mtd/nand/raw/sunxi_nand.c") if args.kernel else
              Path(__file__).parent / "reference/sunxi-partition-map.c").read_text()
    code, _, hashes = bodies({"sunxi_nand.c": source}, {"sunxi_nand.c": [
        "sunxi_boot0_release", "sunxi_boot0_cleanup", "sunxi_boot0_register",
        "sunxi_nand_boot0_partition", "sunxi_nand_has_boot0_partitions",
        "sunxi_nand_check_partitions", "sunxi_nand_register",
    ]})
    if args.save_extracted:
        args.save_extracted.write_text(code + "\n")
    fixture = (Path(__file__).parent / "tests/boot0_partitions.c").read_text()
    check("boot0-partitions", {"check.c": fixture.replace("/* @driver@ */", code)},
          hashes, expected="BOOT0 PARTITIONS PASS", limits=[
              "Extracts actual partition selection, validation, BootROM registration and cleanup",
              "Parser output, MTD publication and allocator failures are simulated",
              "Checks geometry, flags, non-aliasing, all-BootROM layouts and failure cleanup",
              "Does not prove parser internals, concurrent lifetime, hardware ECC or DT handoff",
          ])


if __name__ == "__main__":
    main()
