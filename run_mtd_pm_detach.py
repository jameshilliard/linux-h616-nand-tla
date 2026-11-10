#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check queued MTD PM callbacks against backing-data retirement."""
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
    pm = (tree / "drivers/base/power/main.c").read_text()
    bodies = {name: extract(core, name) for name in
              ("mtd_device_detach", "mtd_device_release", "add_mtd_device")}
    detach = bodies["mtd_device_detach"]
    assert detach.index("guard(device)") < detach.index("dev_set_drvdata(dev, NULL)")
    assert "dev->groups = NULL" in detach
    assert "mtd_class.pm =" not in core
    assert "static const struct device_type mtd_devtype" in shell
    assert "DEFINE_SIMPLE_DEV_PM_OPS(mtd_cls_pm_ops" in shell
    assert ".pm = pm_sleep_ptr(&mtd_cls_pm_ops)" in shell
    for name in ("mtd_cls_suspend", "mtd_cls_resume"):
        bodies[name] = extract(shell, name)
        assert "dev_get_drvdata(dev)" in bodies[name]
    release = bodies["mtd_device_release"]
    assert release.index("device_del") < release.index("mtd_device_detach")
    assert release.index("mtd_device_detach") < release.index("mtd_put_data")
    assert "mtd_device_detach(dev)" in bodies["add_mtd_device"].split("fail_added:")[1]
    for name in ("device_suspend", "device_resume"):
        body = bodies[name] = extract(pm, name)
        assert body.index("device_lock(dev)") < body.index("dev->class->pm")
        assert body.index("dev->class->pm") < body.index("dpm_run_callback")
        assert body.index("dpm_run_callback") < body.index("device_unlock(dev)")
    return dict(kernel_commit=subprocess.check_output(["git", "rev-parse", "HEAD"],
                cwd=tree, text=True).strip(),
                dirty=bool(subprocess.check_output(["git", "diff", "--name-only"], cwd=tree)),
                functions={name: hashlib.sha256(body.encode()).hexdigest()
                           for name, body in bodies.items()},
                limits="Device-lock and callback-selection contract, not the full PM "
                       "scheduler, hardware suspend, or a C/LKMM refinement proof")


def cases():
    base = dict(Tasks=2, DrainUnderLock=True, ClearPointers=True)
    result = [dict(name=f"mtd-pm-detach-{tasks}", module="MtdPMDetach", kind="positive",
                   expected=None, constants=dict(base, Tasks=tasks)) for tasks in (1, 2)]
    for field in ("DrainUnderLock", "ClearPointers"):
        result.append(dict(name="mtd-pm-detach-missing-" + field, module="MtdPMDetach",
                           kind="mutation", expected="MetadataAlive",
                           constants=dict(base, **{field: False})))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--kernel-tree", type=Path)
    args, remaining = parser.parse_known_args()
    metadata = source_check(args.kernel_tree) if args.kernel_tree else dict(
        source_check="Not run; use --kernel-tree to check current sources")
    raise SystemExit(tlc.main(remaining, cases=cases(), toolchain=TLC, metadata=metadata,
        modules={"MtdPMDetach": dict(properties=[],
            invariants=["TypeOK", "MetadataAlive", "CallbackResident", "DeviceAlive"],
            actions=["Queue", "BeginRemove", "DeviceDel", "EnterPM", "ReturnPM", "Detach",
                     "ReleaseData", "UnloadCore", "FinalDevicePut"])}))
