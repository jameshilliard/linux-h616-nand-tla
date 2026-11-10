#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded concat publication/removal model with ownership negative controls."""
import sys
import run as tlc
from mtdchar_cases import TLC

constants = dict(RetainBacking=True, RetainComponents=True,
                 RequireReady=True, ClearFailed=True)
cases = [dict(name="concat-fixed", module="MtdConcatLifetime", kind="positive",
              expected=None, constants=constants)]
for field, invariant in (("RetainBacking", "BackingStorage"),
                         ("RetainComponents", "ComponentStorage"),
                         ("RequireReady", "RegistrationComplete"),
                         ("ClearFailed", "FailedProbeUnstaged")):
    cases.append(dict(name="concat-missing-" + field, module="MtdConcatLifetime",
                      kind="mutation", expected=invariant,
                      constants=dict(constants, **{field: False})))

if __name__ == "__main__":
    raise SystemExit(tlc.main(sys.argv[1:], cases=cases, toolchain=TLC,
        modules={"MtdConcatLifetime": dict(
            invariants=["BackingStorage", "ComponentStorage", "RegistrationComplete",
                        "FailedProbeUnstaged", "SnapshotOrder"],
            actions=["Probe", "Ready", "Publish", "Acquire", "Unbind", "FailProbe",
                     "FilePut", "NvmemPut", "DeviceRelease", "PartRelease",
                     "BackingRelease"])}))
