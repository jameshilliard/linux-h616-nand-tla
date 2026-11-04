#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute MTD reference/partition removal through sunxi chip cleanup."""
import argparse
from pathlib import Path
from boundary_common import HERE, sources, bodies, check

FUNCTIONS = {
    "mtd.h": ["mtd_get_master", "mtd_get_master_ofs"],
    "mtdcore.c": ["mtd_release", "mtd_device_release", "del_mtd_device", "__get_mtd_device",
                  "__put_mtd_device", "mtd_device_unregister", "get_mtd_device", "mtd_read_oob_std"],
    "mtdpart.c": ["__del_mtd_partitions", "del_mtd_partitions"],
    "sunxi_nand.c": ["sunxi_nand_chips_cleanup"],
}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source", type=Path)
    args = parser.parse_args()
    src = sources(args.candidate_source)
    code, proto, hashes = bodies(src, FUNCTIONS)
    check("lifetime-boundary", {"check.c":
        (HERE/"tests/boundary_list.h").read_text() +
        (HERE/"tests/lifetime_boundary.h").read_text() + proto + "\n" + code +
        '\n#line 1 "lifetime_boundary.c"\n' +
        (HERE/"tests/lifetime_boundary.c").read_text()}, hashes,
        coverage=(src, FUNCTIONS), limits=[
            "Single-thread event schedules; no real device-core/sysfs unbind execution",
            "Fixture retains object storage after release to inspect ownership safely",
            "Module references are not a proof that sysfs driver unbind is excluded"])
