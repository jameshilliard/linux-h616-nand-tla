#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute a candidate retained-handle implementation, not a policy surrogate."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from boundary_common import HERE, bodies, check
from run_core import extract

FILES = {
    "mtd.h": "include/linux/mtd/mtd.h",
    "mtdcore.c": "drivers/mtd/mtdcore.c",
    "mtdpart.c": "drivers/mtd/mtdpart.c",
    "mtdchar.c": "drivers/mtd/mtdchar.c",
    "nand_base.c": "drivers/mtd/nand/raw/nand_base.c",
    "nand_lifetime.c": "drivers/mtd/nand/raw/nand_lifetime.c",
    "sunxi_nand.c": "drivers/mtd/nand/raw/sunxi_nand.c",
}
FUNCTIONS = {
    "mtd.h": ["mtd_get_master", "mtd_sync"],
    "mtdcore.c": ["__mtd_next_device", "mtd_master_release", "mtd_get_lifetime",
                  "mtd_put_lifetime", "mtd_set_release", "mtd_put_master",
                  "mtd_release", "mtd_device_release", "del_mtd_device",
                  "get_mtd_device", "__get_mtd_device", "__put_mtd_device",
                  "put_mtd_device", "mtd_device_unregister", "mtd_dev_release_done",
                  "mtd_otp_nvmem_get", "mtd_otp_nvmem_put", "mtd_otp_nvmem_remove"],
    "mtdpart.c": ["__del_mtd_partitions", "del_mtd_partitions"],
    "mtdchar.c": ["mtdchar_open", "mtdchar_close"],
    "nand_base.c": ["nand_get_device", "nand_release_device", "nand_disconnect",
                    "nand_sync", "nand_suspend", "nand_resume"],
    "nand_lifetime.c": ["nand_controller_release", "nand_controller_put",
                        "devm_nand_controller_init", "nand_mtd_get_device",
                        "nand_mtd_release", "nand_set_release"],
    "sunxi_nand.c": ["sunxi_nfc_release",
                     "sunxi_nand_release", "sunxi_nand_chips_cleanup"],
}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-tree", type=Path)
    parser.add_argument("--update-reference", action="store_true",
                        help="snapshot the candidate's exact function bodies")
    args = parser.parse_args()
    reference = HERE / "reference/retained-lifetime.json"
    if args.update_reference and not args.kernel_tree:
        parser.error("--update-reference requires --kernel-tree")
    if args.kernel_tree:
        src = {name: (args.kernel_tree / path).read_text() for name, path in FILES.items()}
        if "mtd_otp_nvmem_remove(" not in src["mtdcore.c"]:
            FUNCTIONS["mtdcore.c"].remove("mtd_otp_nvmem_remove")
        if args.update_reference:
            if subprocess.check_output(["git", "status", "--porcelain"], cwd=args.kernel_tree):
                parser.error("commit the candidate before snapshotting it")
            payload = dict(commit=subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=args.kernel_tree, text=True).strip(), files={})
            for name, names in FUNCTIONS.items():
                functions = {fn: extract(src[name], fn) for fn in names}
                payload["files"][name] = dict(path=FILES[name],
                    source_sha256=hashlib.sha256(src[name].encode()).hexdigest(),
                    functions=functions, functions_sha256={fn:hashlib.sha256(body.encode()).hexdigest()
                                                          for fn,body in functions.items()})
            reference.write_text(json.dumps(payload, indent=2) + "\n")
    else:
        payload = json.loads(reference.read_text())
        if "mtd_otp_nvmem_remove" not in payload["files"]["mtdcore.c"]["functions"]:
            FUNCTIONS["mtdcore.c"].remove("mtd_otp_nvmem_remove")
        src = {}
        for name, names in FUNCTIONS.items():
            entry = payload["files"][name]
            assert set(entry["functions"]) == set(names)
            for fn, body in entry["functions"].items():
                assert hashlib.sha256(body.encode()).hexdigest() == entry["functions_sha256"][fn]
            src[name] = "\n\n".join(entry["functions"].values())
    code, proto, hashes = bodies(src, FUNCTIONS)
    # Retain the historical pin as a control while checking revised callbacks.
    external_owner = "__module_get" not in extract(src["mtdcore.c"], "mtd_otp_nvmem_get")
    external_device_owner = "module_put(THIS_MODULE)" not in extract(src["mtdcore.c"], "mtd_release")
    disconnected_pm_ok = "-ENODEV" not in extract(src["nand_base.c"], "nand_suspend")
    otp_cleanup_helper = "mtd_otp_nvmem_remove" in FUNCTIONS["mtdcore.c"]
    defines = (f"#define EXTERNAL_OTP_OWNER {int(external_owner)}\n"
               f"#define EXTERNAL_DEVICE_OWNER {int(external_device_owner)}\n"
               f"#define DISCONNECTED_PM_OK {int(disconnected_pm_ok)}\n"
               f"#define HAS_OTP_CLEANUP_HELPER {int(otp_cleanup_helper)}\n")
    check("retained-lifetime", {"check.c":
        defines +
        (HERE / "tests/boundary_list.h").read_text() +
        (HERE / "tests/retained_lifetime.h").read_text() + proto + "\n" + code +
        '\n#line 1 "retained_lifetime.c"\n' +
        (HERE / "tests/retained_lifetime.c").read_text()}, hashes,
        coverage=(src, FUNCTIONS), require_all_called=True, limits=[
            "Actual candidate get/put, close/sync, disconnect and release function bodies",
            "Heap objects are freed, so ASan detects premature object access",
            "Registration, device refs, module refs and devres are explicit host shims",
            "Repeated OTP cleanup is checked only for the historical idempotent helper",
            "Sequential schedules; SMP lock/disconnect interleavings checked separately by TLC",
            "Does not cover virtual concatenation, real sysfs unbind or hardware execution"])
