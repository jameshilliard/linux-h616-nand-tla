# SPDX-License-Identifier: GPL-2.0-or-later
"""Availability and publication ordering; source-backed candidate checks are separate."""


def extend_probe(modules, add, mutation):
    modules.update({
        "NandChildAdmission": dict(
            invariants=["OnlyAvailableScanned", "OnlyAvailableClaimed",
                        "PublishedScanned", "FailureUnwinds"],
            actions=["NextNode", "Claim", "Scan", "Register", "Rollback"]),
        "NandProbePublication": dict(
            invariants=["NoContextInterference", "PublishedScanned", "UserOwnsPublishedChip"],
            actions=["ConfigureScan", "ScanCommand", "NextScan", "Register",
                     "UserBegin", "UserSelect", "UserTransfer", "Finish"]),
    })

    def node(status="okay", cs=0, present=True, registerOK=True):
        return dict(status=status, cs=cs, present=present, registerOK=registerOK)

    for status in ("absent", "okay", "ok", "disabled", "reserved", "fail", "fail-1"):
        for present in (False, True):
            for first in (False, True):
                nodes = (node(status, 0, present), node(cs=1))
                if not first:
                    nodes = tuple(reversed(nodes))
                add("NandChildAdmission", f"admission-{status}-{present}-{first}",
                    dict(Nodes=nodes, FilterUnavailable=True))
    for nodes, name in (((node(), node(cs=0)), "duplicate"),
                        ((node(), node(cs=1, registerOK=False)), "registration"),
                        ((node("disabled"), node("reserved", cs=1)), "all-disabled"),
                        ((), "empty")):
        add("NandChildAdmission", "admission-" + name, dict(Nodes=nodes, FilterUnavailable=True))
    for present in (False, True):
        add("NandChildAdmission", f"admission-current-disabled-{present}",
            dict(Nodes=(node(), node("disabled", cs=1, present=present)), FilterUnavailable=False),
            kind="source-contract", expected="OnlyAvailableScanned")

    for n in (1, 2, 3):
        for fail_scan in range(n + 1):
            for fail_register in range(n + 1):
                add("NandProbePublication", f"publication-deferred-{n}-{fail_scan}-{fail_register}",
                    dict(N=n, DeferredPublication=True, FailScan=fail_scan,
                         FailRegister=fail_register, Users=True))
        add("NandProbePublication", f"publication-current-no-users-{n}",
            dict(N=n, DeferredPublication=False, FailScan=0, FailRegister=0, Users=False))
    add("NandProbePublication", "publication-current-single-child",
        dict(N=1, DeferredPublication=False, FailScan=0, FailRegister=0, Users=True))
    for n in (2, 3):
        for fail_scan in (0, 2):
            add("NandProbePublication", f"publication-current-race-{n}-{fail_scan}",
                dict(N=n, DeferredPublication=False, FailScan=fail_scan,
                     FailRegister=0, Users=True),
                kind="source-contract", expected="NoContextInterference")
