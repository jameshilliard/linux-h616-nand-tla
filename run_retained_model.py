#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explore candidate NAND disconnect/reference transitions and negative controls."""
import sys
import run as tlc
from mtdchar_cases import TLC

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
    for chips in (1, 2):
        result.append(dict(name=f"controller-{chips}-fixed", module="NandControllerLifetime",
            kind="positive", expected=None, constants=dict(Chips=chips, RetainController=True,
                RetainDevices=True, Drain=True, ReuseSlot=False)))
    for name, field, value, expected in (
            ("early-controller-free", "RetainController", False, "ControllerStorage"),
            ("early-chip-free", "RetainDevices", False, "MetadataStorage"),
            ("no-drain", "Drain", False, "HardwareDrained"),
            ("reused-card-storage", "ReuseSlot", True, "HandleIsolation")):
        constants = dict(Chips=1, RetainController=True, RetainDevices=True,
                         Drain=True, ReuseSlot=False)
        constants[field] = value
        result.append(dict(name="controller-" + name, module="NandControllerLifetime",
            kind="mutation", expected=expected, constants=constants))
    otp = dict(Providers=2, MtdRegistered=True, RetainContext=True,
               ReleaseEarly=False, ClearPointers=True)
    for providers in (1, 2):
        for registered in (False, True):
            result.append(dict(name=f"otp-{providers}-{int(registered)}",
                module="MtdOtpLifetime", kind="positive", expected=None,
                constants=dict(otp, Providers=providers, MtdRegistered=registered)))
    for name, changes, expected in (
            ("missing-context-pin", dict(RetainContext=False), "CallbackStorage"),
            ("early-context-put", dict(ReleaseEarly=True), "CallbackStorage"),
            ("double-unregister", dict(MtdRegistered=False, ClearPointers=False),
             "NoDoubleUnregister")):
        result.append(dict(name="otp-" + name, module="MtdOtpLifetime",
            kind="mutation", expected=expected, constants=dict(otp, **changes)))
    return result

if __name__ == "__main__":
    raise SystemExit(tlc.main(sys.argv[1:], cases=cases(), toolchain=TLC,
        modules={"NandRetainedLifetime": MODEL,
                 "MtdOtpLifetime": dict(invariants=["CallbackStorage", "NoDoubleUnregister"],
                     actions=["Unregister", "OuterUnwind", "DropMtd", "DropInitial",
                              "ConsumerPut", "DevicePut", "ProviderRelease", "BackingRelease"]),
                 "MtdRetainedNumber": dict(invariants=["NoNameCollision"],
                     actions=["LastMtdPut", "ConsumerPut", "ProviderRelease",
                              "DeviceRelease", "RegisterReplacement"]),
                 "NandControllerLifetime": dict(invariants=["MetadataStorage", "ControllerStorage",
                     "HardwareDrained", "HandleIsolation", "Exclusive"], actions=["Read", "Complete",
                     "Put", "Unpublish", "Disconnect", "DropChip", "DevicePut", "ChipRelease",
                     "HardwareOff", "DropBinding", "ControllerRelease", "RegisterReplacement"])}))
