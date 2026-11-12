#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Model legacy provider ownership and disconnect-time GPIO retirement."""
import argparse
from pathlib import Path

import run as tlc
from mtdchar_cases import TLC
from run_nvmem_disconnect import source_check


def cases():
    base = dict(Managed=False, KeepModulePins=True, ReleaseGPIOEarly=True)
    result = [dict(name=f"nvmem-resources-{'managed' if managed else 'explicit'}",
                   module="NvmemResourceRetirement", kind="positive", expected=None,
                   constants=dict(base, Managed=managed)) for managed in (False, True)]
    for name, delta, invariant in (
            ("unpin-managed", dict(Managed=True, KeepModulePins=False), "CallbackStorage"),
            ("late-gpio", dict(ReleaseGPIOEarly=False), "ReprobeAvailable")):
        result.append(dict(name=f"nvmem-resources-{name}", module="NvmemResourceRetirement",
                           kind="mutation", expected=invariant, constants=dict(base, **delta)))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--kernel-tree", type=Path)
    args, remaining = parser.parse_known_args()
    metadata = source_check(args.kernel_tree) if args.kernel_tree else dict(
        source_check="Not run; pass --kernel-tree for structural correspondence")
    metadata["limits"] = (
        "Bounded unload and explicit-unregister ordering; module, SRCU and GPIO "
        "contracts assumed. Legacy explicit unbind before managed unregister "
        "remains outside coverage; no C refinement or weak-memory proof.")
    raise SystemExit(tlc.main(remaining, cases=cases(), toolchain=TLC,
        metadata=metadata, modules={"NvmemResourceRetirement": dict(
            invariants=["TypeOK", "CallbackStorage", "RetainedStorage", "ReprobeAvailable"],
            actions=["Enter", "Check", "Return", "Exit", "Put", "BeginRemove",
                     "ManagedCleanup", "Unregister", "Drain", "FinishRemove", "Reprobe",
                     "BarePut", "FinalRelease"])}))
