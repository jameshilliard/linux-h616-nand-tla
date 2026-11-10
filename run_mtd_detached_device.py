#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the detached MTD class-device prototype's bounded reference budget."""
import argparse
import hashlib
from pathlib import Path
import subprocess

import run as tlc
from mtdchar_cases import TLC
from run_core import extract


def source_check(tree):
    core = (tree / "drivers/mtd/mtdcore.c").read_text()
    shell = (tree / "drivers/mtd/mtddevice.c").read_text()
    device = (tree / "include/linux/device.h").read_text()
    functions = {name: extract(core, name) for name in
                 ("mtd_data_release", "mtd_device_release", "add_mtd_device")}
    functions["mtd_class_device_release"] = extract(shell, "mtd_class_device_release")
    assert "release_owner" not in device
    assert "obj-$(CONFIG_MTD_DEVICE)" in (tree / "drivers/Makefile").read_text()
    assert "select MTD_DEVICE" in (tree / "drivers/mtd/Kconfig").read_text()
    assert "struct class mtd_class =" in shell
    assert "struct class mtd_class =" not in core
    assert "mtd_class_device_alloc" in functions["add_mtd_device"]
    release = functions["mtd_device_release"]
    assert release.index("nvmem_unregister") < release.index("device_del")
    assert release.index("device_del") < release.index("dev_set_drvdata")
    assert release.index("dev_set_drvdata") < release.index("mtd_put_data")
    assert "mtd_put_data(parent)" in functions["mtd_data_release"]
    final = functions["mtd_class_device_release"]
    assert "kfree(dev)" in final and "of_node_put" in final
    assert "module_put" not in final and "drvdata" not in final
    return dict(kernel_commit=subprocess.check_output(["git", "rev-parse", "HEAD"],
                cwd=tree, text=True).strip(),
                dirty=bool(subprocess.check_output(["git", "diff", "--name-only"], cwd=tree)),
                functions={name: hashlib.sha256(body.encode()).hexdigest()
                           for name, body in functions.items()},
                limits="Bounded reference budget and structural source correspondence; "
                       "not a C refinement or full NAND/controller teardown proof")


def cases():
    base = dict(Depth=3, FailInit=False, KeepParentData=True,
                EmbedDevice=False, LateCoreHook=False, PinAPI=True, ClassBuiltIn=True)
    result = [dict(name=f"mtd-detached-{depth}-{'rollback' if fail else 'unbind'}",
                   module="MtdDetachedDevice", kind="positive", expected=None,
                   constants=dict(base, Depth=depth, FailInit=fail))
              for depth in (2, 3) for fail in (False, True)]
    for name, delta, invariant in (
            ("missing-parent-ref", dict(KeepParentData=False), "ParentData"),
            ("embedded-storage", dict(EmbedDevice=True), "DeviceStorage"),
            ("late-core-hook", dict(LateCoreHook=True), "CoreOnlyRelease"),
            ("unprotected-api", dict(PinAPI=False), "APIResident"),
            ("module-static-class", dict(ClassBuiltIn=False), "ClassStorage")):
        result.append(dict(name=f"mtd-detached-{name}", module="MtdDetachedDevice",
                           kind="mutation", expected=invariant, constants=dict(base, **delta)))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--kernel-tree", type=Path)
    args, remaining = parser.parse_known_args()
    metadata = source_check(args.kernel_tree) if args.kernel_tree else dict(
        source_check="Not run; pass --kernel-tree to check the prototype source",
        limits="Bounded reference budget, not a C refinement or full teardown proof")
    raise SystemExit(tlc.main(remaining, cases=cases(), toolchain=TLC,
        metadata=metadata, modules={"MtdDetachedDevice": dict(
            invariants=["TypeOK", "DeviceStorage", "ParentData", "ProviderStorage",
                        "CoreOnlyRelease", "APIResident", "ClassStorage"],
            actions=["Unbind", "Close", "DropHidden", "RemoveInterface", "ReleaseData",
                     "ReturnAPI", "FreeContext", "UnloadProvider", "UnloadCore",
                     "ClassUnregister", "ClassRelease", "BarePut", "QueueRelease",
                     "DeviceRelease"])}))
