#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise sunxi partition-format selection and registration without flash I/O."""
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
        "sunxi_nand_boot_release", "sunxi_nand_boot_cleanup", "sunxi_nand_boot_register",
        "sunxi_nand_boot_partition", "sunxi_nand_has_boot_partitions",
        "sunxi_nand_check_partitions", "sunxi_nand_register",
    ]})
    if args.save_extracted:
        args.save_extracted.write_text(code + "\n")
    fixture = (Path(__file__).parent / "tests/boot_format_partitions.c").read_text()
    check("boot-format-partitions", {"check.c": fixture.replace("/* @driver@ */", code)},
          hashes, expected="BOOT FORMAT PARTITIONS PASS", limits=[
              "Extracts actual partition selection, validation, BootROM registration and cleanup",
              "Parser output, MTD publication and allocator failures are simulated",
              "Checks geometry, flags, non-aliasing, all-BootROM layouts and failure cleanup",
              "Does not prove parser internals, concurrent lifetime, hardware ECC or DT handoff",
          ])


if __name__ == "__main__":
    main()
