#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check source-extracted BootROM teardown ordering with bounded waiters."""
import argparse
from pathlib import Path
import subprocess

from boundary_common import HERE, bodies, check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--revision")
    parser.add_argument("--expect-deadlock", action="store_true")
    args = parser.parse_args()
    name = "drivers/mtd/nand/raw/sunxi_nand.c"
    source = (subprocess.check_output(["git", "show", f"{args.revision}:{name}"],
                                     cwd=args.kernel, text=True) if args.revision else
              (args.kernel / name).read_text())
    code, _, hashes = bodies({"sunxi_nand.c": source}, {"sunxi_nand.c": [
        "to_sunxi_nand_boot", "sunxi_nand_boot_isbad", "sunxi_nand_boot_release",
        "sunxi_nand_boot_cleanup", "sunxi_nand_chips_cleanup",
    ]})
    # MTD core already rejects new handles after unregistration. A view's
    # admission callback must not wait for its I/O lock under mtd_table_mutex.
    if not args.expect_deadlock:
        assert "mtd->_get_device = sunxi_nand_boot_get_device;" not in source
    fixture = (HERE / "tests/boot_format_lifetime.c").read_text()
    unit = (HERE / "tests/boundary_list.h").read_text() + fixture.replace("/* @driver@ */", code)
    check("boot-format-lifetime", {"check.c": unit}, hashes,
          flags=[f"-DEXPECT_DEADLOCK={int(args.expect_deadlock)}"],
          expected="BOOT FORMAT DEADLOCK REPRODUCED" if args.expect_deadlock else
                   "BOOT FORMAT LIFETIME CHECKS PASS",
          limits=[
              "Actual BootROM and native-chip cleanup bodies with bounded lock schedules",
              "A held view lock models a request sleeping in nand_get_device()",
              "NAND disconnection/wakeup, MTD refs and registration are explicit API shims",
              "Checks chip-before-view drain ordering, disconnected handles and final release",
              "Does not prove generic MTD/NVMEM teardown, SMP behavior or hardware removal",
          ])


if __name__ == "__main__":
    main()
