# SPDX-License-Identifier: GPL-2.0-or-later
"""Backend-specific recovery, timing handoff, and compound-failure obligations."""


def extend_validation(modules, add, mutation):
    modules.update({
        "NandRelocation": dict(invariants=["RetainOld", "AckDurable", "RejectTorn", "SeparateCopies"],
            actions=["Select", "Erase", "Program", "Tear", "Advance", "Cut", "Recover"]),
        "NandRecovery": dict(invariants=["SafeMemory", "SafeCommandReuse", "NoOldCompletion", "BackendSelection"],
            actions=["Event", "IrqBegin", "IrqFinish", "Memory", "Timeout", "Drain", "Clear", "Abort", "Cleanup", "Reuse", "Settle"]),
        "NandNegotiation": dict(invariants=["Bounds", "ModeAgreement", "RecoveryError"],
            actions=["HostReset", "ChipReset", "SetFeature", "HostSetup", "ReadAck", "RecoverHost", "RecoverChip"]),
    })
    recovery=dict(Backend="descriptor",StopSucceeds=True,BusDrains=True,CommandStops=True,EventsStop=True)
    for backend in ("pio", "sun4i", "sun6i", "register", "descriptor"):
        add("NandRecovery", "validation-recovery-"+backend, dict(recovery,Backend=backend))
        add("NandRecovery", "validation-recovery-late-event-"+backend,
            dict(recovery,Backend=backend,EventsStop=False),kind="assumption",expected="NoOldCompletion")
        add("NandRecovery", "validation-recovery-command-active-"+backend,
            dict(recovery,Backend=backend,CommandStops=False),kind="assumption",expected="SafeCommandReuse")
        if backend!="pio":
            for label,change in (("posted-write",dict(BusDrains=False)),("stop-failed",dict(StopSucceeds=False))):
                add("NandRecovery",f"validation-recovery-{label}-{backend}",
                    dict(recovery,Backend=backend,**change),kind="assumption",expected="SafeMemory")
    mutation("NandRecovery",recovery,"skip-irq-drain","NoOldCompletion")
    negotiation=dict(BootMode=5,Desired=4,Features="get",AckMatches=True,Fault="none")
    for boot in (0,3,5):
        for features in ("none","set","get"):
            for desired in (0,2,5):
                add("NandNegotiation",f"validation-onfi-boot{boot}-{features}-mode{desired}",
                    dict(negotiation,BootMode=boot,Features=features,Desired=desired))
    for fault in ("none","set","host","get"):
        add("NandNegotiation","validation-onfi-mismatch-"+fault,dict(negotiation,AckMatches=False,Fault=fault))
    for fault in ("recover-host","recover-chip"):
        add("NandNegotiation","validation-onfi-source-"+fault,dict(negotiation,AckMatches=False,Fault=fault),
            kind="source-contract",expected="RecoveryError")
    for mode in ("place","auto","raw"):
        add("NandRequest","validation-compound-retry-transport-data-"+mode,
            dict(Pages=3,Oob=False,Retry=True,FailPage=1,TransportPage=2,Mode=mode))
    for mode in ("place","auto"):
        add("NandRequest","validation-compound-retry-transport-oob-"+mode,
            dict(Pages=3,Oob=True,Retry=True,FailPage=1,TransportPage=2,Mode=mode),
            kind="source-contract",expected="OobErrorPrefix")
    for erase in (set(),{1},{2},{1,3}):
        for program in (set(),{3},{3,4},{2,4}):
            add("NandRelocation",f"validation-relocate-erase{sorted(erase)}-program{sorted(program)}",
                dict(EraseFails=erase,ProgramFails=program,DetectTorn=True))
    relocation=dict(EraseFails={1},ProgramFails={3},DetectTorn=True)
    mutation("NandRelocation",relocation,"overwrite-mirror","RetainOld")
    add("NandRelocation","validation-relocate-undetected-tear",dict(relocation,DetectTorn=False),
        kind="assumption",expected="RejectTorn")
