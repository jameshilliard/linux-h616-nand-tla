#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the provider-detach experiment without changing sent-series snapshots."""
import argparse
import hashlib
import subprocess
from pathlib import Path

import run as tlc
from mtdchar_cases import TLC
from run_core import extract


def source_check(tree):
    source = (tree / "drivers/nvmem/core.c").read_text()
    names = ("nvmem_reg_read", "nvmem_reg_write", "__nvmem_cell_read",
             "__nvmem_add_cells_from_dt", "nvmem_disconnect", "nvmem_unregister",
             "nvmem_release", "nvmem_device_match", "nvmem_device_put")
    bodies = {name: extract(source, name) for name in names}
    for name in names[:4]:
        body = bodies[name]
        assert body.index("guard(srcu)") < body.index("READ_ONCE(nvmem->disconnected)")
        assert "return -ENODEV;" in body
    body = bodies["nvmem_disconnect"]
    assert body.index("WRITE_ONCE(nvmem->disconnected, true)") < body.index("synchronize_srcu")
    body = bodies["nvmem_unregister"]
    assert body.index("nvmem_disconnect") < body.index("device_del") < body.index("kref_put")
    assert "priv_put" not in bodies["nvmem_release"]
    assert "module_put" not in bodies["nvmem_release"]
    assert "try_module_get(nvmem->owner)" in bodies["nvmem_device_match"]
    assert "module_put(nvmem->owner)" in bodies["nvmem_device_put"]
    disconnect = bodies["nvmem_disconnect"]
    assert disconnect.index("synchronize_srcu") < disconnect.index("gpiod_put")
    assert "gpiod_put" not in bodies["nvmem_release"]
    return dict(kernel_commit=subprocess.check_output(["git", "rev-parse", "HEAD"],
                cwd=tree, text=True).strip(),
                dirty=bool(subprocess.check_output(["git", "diff", "--name-only"], cwd=tree)),
                functions={name: hashlib.sha256(body.encode()).hexdigest()
                           for name, body in bodies.items()},
                limits="Structural source correspondence and bounded SRCU contract model; "
                       "not a C refinement, weak-memory proof or complete MTD lifetime model")


def cases():
    base = dict(Readers=1, DrainCallbacks=True, ProtectPostprocess=True,
                RejectLate=True, LateReleaseHook=False)
    result = [dict(name=f"nvmem-detach-{readers}", module="NvmemDisconnect",
                   kind="positive", expected=None, constants=dict(base, Readers=readers))
              for readers in (1, 2)]
    for name, delta, invariant in (
            ("no-drain", dict(DrainCallbacks=False), "CallbackStorage"),
            ("postprocess-outside", dict(ProtectPostprocess=False), "CallbackStorage"),
            ("late-admission", dict(RejectLate=False), "CallbackStorage"),
            ("late-provider-release", dict(LateReleaseHook=True), "CoreOnlyRelease")):
        result.append(dict(name=f"nvmem-detach-{name}", module="NvmemDisconnect",
                           kind="mutation", expected=invariant, constants=dict(base, **delta)))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--kernel-tree", type=Path)
    args, remaining = parser.parse_known_args()
    metadata = source_check(args.kernel_tree) if args.kernel_tree else dict(
        source_check="Not run; pass --kernel-tree to check the prototype source",
        limits="Bounded SRCU contract model, not a C refinement or weak-memory proof")
    raise SystemExit(tlc.main(remaining, cases=cases(), toolchain=TLC,
        metadata=metadata, modules={"NvmemDisconnect": dict(
            invariants=["TypeOK", "CallbackStorage", "CoreStorage", "CoreOnlyRelease"],
            actions=["Enter", "Check", "CallbackReturn", "PostStart", "PostReturn", "Exit",
                     "Put", "Disconnect", "Drain", "FreeContext", "Unload", "BarePut", "DeviceRelease"])}))
