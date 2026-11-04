# SPDX-License-Identifier: GPL-2.0-or-later
"""Refinements: candidate policies and adverse hardware contracts stay labelled."""


def extend_next(modules, add, mutation):
    modules.update({
        "NandRecoveryRegisters": dict(invariants=["MappingSafe", "FreshContext",
            "NoStaleEvent", "CorrectBackend", "FailedStopRetained"],
            actions=["LateWrite", "LateEvent", "Timeout", "Abort", "Cleanup",
                     "Select", "Transfer", "Finish"]),
        "NandDetachPolicy": dict(invariants=["ResourcesForIO", "NoNewOpen",
            "PartitionPinsMaster", "AllReleased"], actions=["Lookup", "Acquire",
            "StartIO", "FinishIO", "Close", "Unregister", "Cleanup", "Finish"]),
        "NandDmaBytes": dict(invariants=["NeighborPreserved", "NoOldDma", "PayloadOnly",
            "DeliveredPayload", "PublishedPayload"],
            actions=["Map", "NeighborWrite", "DeviceTransfer", "Stop", "Unmap", "Reuse", "Finish"]),
        "NandRetryProtocol": dict(invariants=["ReadUsesAppliedParameters", "NoFalseDefault",
            "RestoreOnSuccess"], actions=["Step", "Read", "Return"]),
    })
    recovery = dict(Backend="descriptor", StopOK=True, RetainRegisters=False,
                    RetireWrites=True, RetireEvents=True, Policy="source", NextChip=1)
    for backend in ("pio", "sun4i", "sun6i", "register", "descriptor"):
        for retained in (False, True):
            for chip in (1, 2):
                add("NandRecoveryRegisters", f"refine-recovery-{backend}-{retained}-chip{chip}",
                    dict(recovery, Backend=backend, RetainRegisters=retained, NextChip=chip))
        if backend != "pio":
            add("NandRecoveryRegisters", f"refine-recovery-{backend}-failed-stop",
                dict(recovery, Backend=backend, StopOK=False),
                kind="source-contract", expected="MappingSafe")
            add("NandRecoveryRegisters", f"refine-recovery-{backend}-quarantine",
                dict(recovery, Backend=backend, StopOK=False, Policy="quarantine"))
    for name, changes, invariant in (
            ("posted-write", dict(RetireWrites=False), "MappingSafe"),
            ("late-event", dict(RetireEvents=False), "NoStaleEvent")):
        add("NandRecoveryRegisters", "refine-recovery-" + name,
            dict(recovery, **changes), kind="assumption", expected=invariant)
    mutation("NandRecoveryRegisters", recovery, "skip-same-clock", "FreshContext")
    for partition in (False, True):
        for probe_failure in (False, True):
            for same_chip in (False, True):
                for policy in ("gate-and-drain", "defer-to-last-ref", "source"):
                    base = dict(Policy=policy, Partition=partition, ProbeFailure=probe_failure,
                                SameChip=same_chip)
                    add("NandDetachPolicy", f"refine-detach-{policy}-part{partition}-probe{probe_failure}-same{same_chip}",
                        base, **(dict(kind="source-contract", expected="ResourcesForIO")
                                 if policy == "source" else {}))
    base = dict(Policy="gate-and-drain", Partition=True, ProbeFailure=True, SameChip=True)
    mutation("NandDetachPolicy", base, "skip-drain", "ResourcesForIO")
    mutation("NandDetachPolicy", base, "unguarded-open", "NoNewOpen")
    add("NandDetachPolicy", "refine-detach-source-late-open", dict(base, Policy="source"),
        kind="source-contract", expected="NoNewOpen")
    dma = dict(Bounce=True, Aligned=False, NeighborWrites=True, StopOK=True, Direction="rx")
    for direction in ("rx", "tx"):
        for bounce, aligned, neighbors in ((True, False, True), (True, True, True),
                                          (False, True, True), (False, False, False)):
            add("NandDmaBytes", f"refine-dma-{direction}-bounce{bounce}-aligned{aligned}-neighbor{neighbors}",
                dict(dma, Bounce=bounce, Aligned=aligned, NeighborWrites=neighbors, Direction=direction))
    add("NandDmaBytes", "refine-dma-shared-cacheline", dict(dma, Bounce=False),
        kind="assumption", expected="NeighborPreserved")
    add("NandDmaBytes", "refine-dma-old-mapping-write", dict(dma, StopOK=False),
        kind="source-contract", expected="NoOldDma")
    mutation("NandDmaBytes", dma, "whole-line-copyback", "NeighborPreserved")
    mutation("NandDmaBytes", dma, "skip-copyback", "DeliveredPayload")
    mutation("NandDmaBytes", dict(dma, Direction="tx"), "skip-publish", "PublishedPayload")
    for vendor in ("hynix", "micron"):
        last = 6 if vendor == "hynix" else 1
        retry = dict(Vendor=vendor, FailPhase=-1, FailStep=1, AcceptedOnError=False)
        add("NandRetryProtocol", "refine-retry-" + vendor + "-success", retry)
        for phase in (0, 1):
            for step in range(1, last + 1):
                for accepted in (False, True):
                    add("NandRetryProtocol", f"refine-retry-{vendor}-phase{phase}-step{step}-{accepted}",
                        dict(retry, FailPhase=phase, FailStep=step, AcceptedOnError=accepted))
        add("NandRetryProtocol", "refine-retry-" + vendor + "-failed-restore-state",
            dict(retry, FailPhase=0), kind="source-contract", expected="DefaultAtEveryReturn")
        add("NandRetryProtocol", "refine-retry-" + vendor + "-assume-error-rolled-back",
            dict(retry, FailPhase=0, Mutation="assume-error-rolled-back"),
            kind="mutation", expected="NoFalseDefault")
