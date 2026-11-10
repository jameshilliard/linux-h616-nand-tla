#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check module text liveness through final device callback return."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import run as tlc
from mtdchar_cases import TLC
from run_core import extract

HERE = Path(__file__).resolve().parent
FUNCTIONS = {
    "drivers/base/core.c": ("device_initialize", "device_release"),
    "drivers/mtd/mtdcore.c": ("add_mtd_device", "mtd_release"),
}


def check_source(args):
    reference = HERE / "reference/device-release-owner.json"
    if args.kernel_tree:
        payload = dict(commit=subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=args.kernel_tree, text=True).strip(),
            functions={})
        for path, functions in FUNCTIONS.items():
            source = (args.kernel_tree / path).read_text()
            for name in functions:
                body = extract(source, name)
                payload["functions"][name] = dict(path=path, body=body,
                    sha256=hashlib.sha256(body.encode()).hexdigest())
        if args.update_reference:
            if subprocess.check_output(["git", "status", "--porcelain"], cwd=args.kernel_tree):
                raise SystemExit("Commit the candidate before snapshotting it")
            reference.write_text(json.dumps(payload, indent=2) + "\n")
    else:
        payload = json.loads(reference.read_text())
    code = {}
    for name, record in payload["functions"].items():
        body = record["body"]
        assert hashlib.sha256(body.encode()).hexdigest() == record["sha256"]
        code[name] = body
    initialize = code["device_initialize"]
    assert initialize.index("__module_get(dev->release_owner)") < initialize.index("kobject_init(")
    release = code["device_release"]
    cache = release.index("struct module *owner = dev->release_owner;")
    put = release.index("module_put(owner);")
    for callback in ("dev->release(dev);", "dev->type->release(dev);",
                     "dev->class->dev_release(dev);"):
        assert cache < release.index(callback) < put
    assert release.index("kfree(p);") < put
    registration = code["add_mtd_device"]
    assert registration.index("mtd->dev.release_owner = THIS_MODULE;") < registration.index("device_register(")
    assert "module_put(THIS_MODULE)" not in code["mtd_release"]
    assert "__module_get(THIS_MODULE)" not in registration
    return dict(candidate=payload["commit"],
                functions={name: record["sha256"] for name, record in payload["functions"].items()},
                limits="Structural source checks and bounded interleaving model, not C refinement proof")


def cases():
    result = []
    base = dict(Devices=1, Outcome="registered", PinAtInit=True,
                PutAfterReturn=True, CacheOwner=True)
    for devices in (1, 2):
        for outcome in ("initialized", "registered", "failed-add"):
            result.append(dict(name=f"release-{devices}-{outcome}",
                module="DeviceReleaseOwner", kind="positive", expected=None,
                constants=dict(base, Devices=devices, Outcome=outcome)))
    for name, changes, invariant in (
            ("self-put", dict(PutAfterReturn=False), "CallbackCode"),
            ("late-pin", dict(PinAtInit=False, Outcome="failed-add"), "PendingCode"),
            ("uncached-owner", dict(CacheOwner=False), "CachedOwner")):
        result.append(dict(name="release-" + name, module="DeviceReleaseOwner",
            kind="mutation", expected=invariant, constants=dict(base, **changes)))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--kernel-tree", type=Path)
    parser.add_argument("--update-reference", action="store_true")
    args, remaining = parser.parse_known_args()
    if args.update_reference and not args.kernel_tree:
        parser.error("--update-reference requires --kernel-tree")
    metadata = check_source(args)
    raise SystemExit(tlc.main(remaining, cases=cases(), toolchain=TLC, metadata=metadata,
        modules={"DeviceReleaseOwner": dict(
            invariants=["TypeOK", "CallbackCode", "PendingCode", "CachedOwner", "NoLeaks"],
            actions=["Initialize", "Prepare", "Handoff", "LastPut", "Enter",
                     "CallbackBody", "Return", "CorePut", "Unload"])}))
