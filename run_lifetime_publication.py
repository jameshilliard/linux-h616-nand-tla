#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded publication/retirement contracts with independently failing mutations."""
import run as tlc
from mtdchar_cases import TLC


def cases():
    partition = dict(EarlyPublish=False, HoldParseRef=True, RejectRemovedParent=True)
    cell = dict(Refresh=True, ExplicitLayout=True, RemoveBeforeFree=True,
                PairedNotifier=True, ExplicitOwner=True)
    result = []
    for module, base, mutations in (
        ("MtdPartitionPublication", partition, (
            ("early-list", dict(EarlyPublish=True), "ClassDevicePublished"),
            ("no-parse-ref", dict(HoldParseRef=False), "ParserStorage"),
            ("removed-parent", dict(RejectRemovedParent=False), "NoOrphanChild"))),
        ("NvmemCellPublication", cell, (
            ("stale-group", dict(Refresh=False), "AllCellAttributes"),
            ("implicit-layout", dict(ExplicitLayout=False), "ProviderCellsSurvive"),
            ("early-free", dict(RemoveBeforeFree=False), "ActiveAttributesAlive"),
            ("unpaired-remove", dict(PairedNotifier=False), "NotificationsPaired"),
            ("owner-fallback", dict(ExplicitOwner=False), "ControllerNotPinned")))):
        result.append(dict(name=module + "-fixed", module=module, kind="positive",
                           expected=None, constants=base))
        for name, delta, invariant in mutations:
            result.append(dict(name=module + "-" + name, module=module, kind="mutation",
                               expected=invariant, constants=dict(base, **delta)))
    return result


if __name__ == "__main__":
    raise SystemExit(tlc.main(cases=cases(), toolchain=TLC,
        metadata=dict(limits="Bounded API-contract models, not full driver, C refinement, "
                             "weak-memory, or hardware verification."), modules={
            "MtdPartitionPublication": dict(
                invariants=["ClassDevicePublished", "ParserStorage", "NoOrphanChild"],
                actions=["Publish", "Register", "Delete", "Parse", "Put"]),
            "NvmemCellPublication": dict(invariants=["AllCellAttributes", "ProviderCellsSurvive",
                "ActiveAttributesAlive", "NotificationsPaired", "ControllerNotPinned"],
                actions=["Bind", "AddLate", "Unbind", "Rebind", "Publish", "Reader",
                         "Remove", "Drain", "Free"])}))
