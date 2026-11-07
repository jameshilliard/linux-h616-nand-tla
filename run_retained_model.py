#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explore candidate NAND disconnect/reference transitions and negative controls."""
import sys
import run as tlc

MODEL = dict(invariants=["LiveStorage", "ParentStorage", "DrainedHardware", "Exclusive"],
             actions=["Open", "ChipLock", "Check", "WakeWaiter", "ControllerLock",
                      "Unlock", "Put", "Disconnect", "Unpublish", "DropInitial",
                      "Teardown", "DeviceRelease", "MetadataRelease"])

def cases():
    result = []
    for devices in (1, 3):
        for suspended in (False, True):
            result.append(dict(name=f"retained-{devices}-{int(suspended)}",
                module="NandRetainedLifetime", kind="positive", expected=None,
                constants=dict(DeviceCount=devices, InitiallySuspended=suspended,
                               Drain=True, RetainDevices=True, RetainParents=True, Wake=True)))
    for name, field, expected in (("no-drain", "Drain", "DrainedHardware"),
                                   ("no-device-ref", "RetainDevices", "LiveStorage"),
                                   ("no-parent-ref", "RetainParents", "ParentStorage")):
        constants=dict(DeviceCount=3, InitiallySuspended=False,
                       Drain=True, RetainDevices=True, RetainParents=True, Wake=True)
        constants[field]=False
        result.append(dict(name="retained-"+name, module="NandRetainedLifetime",
            kind="mutation", expected=expected, constants=constants))
    result.append(dict(name="retained-no-suspend-wakeup", module="NandRetainedLifetime",
        kind="mutation", expected="Terminates", temporal=True,
        constants=dict(DeviceCount=1, InitiallySuspended=True,
                       Drain=True, RetainDevices=True, RetainParents=True, Wake=False)))
    for fixed in (True, False):
        result.append(dict(name="retained-number-" + ("fixed" if fixed else "early-reuse"),
            module="MtdRetainedNumber", kind="positive" if fixed else "mutation",
            expected=None if fixed else "NoNameCollision", constants=dict(HoldNumber=fixed)))
    return result

if __name__ == "__main__":
    raise SystemExit(tlc.main(sys.argv[1:], cases=cases(),
        modules={"NandRetainedLifetime": MODEL,
                 "MtdRetainedNumber": dict(invariants=["NoNameCollision"],
                     actions=["LastMtdPut", "ConsumerPut", "ProviderRelease",
                              "DeviceRelease", "RegisterReplacement"])}))
