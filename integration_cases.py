# SPDX-License-Identifier: GPL-2.0-or-later
"""NAND-core, persistent format, BBT, coherency and RT integration contracts."""


def extend_integration(modules, add, mutation):
    modules.update({
        "NandRequest": dict(
            invariants=["Lengths", "DataIdentity", "OobIdentity", "RetryStats", "ErrorPrefix"],
            actions=["Read", "Copy", "Interpret", "Advance", "Finish", "Error"]),
        "NandBbt": dict(
            invariants=["Bounds", "NoFalseGood", "MirrorsAgree", "NoIncompletePublish"],
            actions=["Scan", "ScanDone", "WritePrimary", "WriteMirror", "Mark", "Reboot", "Read"]),
        "NandFormat": dict(
            invariants=["MediaFormat", "RoundTrip", "FormatBoundary", "CleanExit"],
            actions=["Configure", "Program", "NextWrite", "Read", "Finish"]),
        "NandOwnership": dict(
            invariants=["DescriptorPublication", "NoUnsafeUnmap", "DeviceVisibility",
                        "RegisterOwnership", "ReturnedOwnership"],
            actions=["CpuPrepare", "Map", "Descriptor", "PublishEarly", "Barrier", "Submit", "Device",
                     "Timeout", "Abort", "Unmap", "Consume"]),
        "NandPersistence": dict(
            invariants=["RetainOld", "AckDurable", "ProgramLimit", "RejectTorn", "NewestAvailable"],
            actions=["Mark", "Erase", "Program", "Tear", "Advance", "Cut", "Recover"]),
        "NandRt": dict(
            invariants=["NoStaleAccess", "DrainBeforeReuse"],
            actions=["ReadStatus", "ReadMask", "Ack", "Mask", "Complete", "Timeout",
                     "Drain", "Check", "Reuse", "Tick"]),
        "NandProgram": dict(
            invariants=["ProgramBudget", "NoResurrection", "Readback", "NoImplicitReplay"],
            actions=["Choose", "Admit", "Reject", "Program", "Tear", "Read", "Erase", "Finish"]),
    })
    request = dict(Pages=3, Oob=True, Retry=False, FailPage=0, TransportPage=0, Mode="place")
    for mode in ("place", "auto", "raw"):
        for oob in (False, True):
            for fault in (0, 2):
                add("NandRequest", f"integrated-request-{mode}-oob{oob}-io{fault}",
                    dict(request, Mode=mode, Oob=oob, TransportPage=fault))
    for oob in (False, True):
        add("NandRequest", f"integrated-request-ecc-oob{oob}", dict(request, Oob=oob, FailPage=2))
    add("NandRequest", "integrated-request-retry-data", dict(request, Oob=False, Retry=True, FailPage=1))
    for mode in ("place", "auto"):
        add("NandRequest", f"integrated-request-source-retry-oob-{mode}",
            dict(request, Mode=mode, Retry=True, FailPage=1),
            expected="OobIdentity", kind="source-contract")
    mutation("NandRequest", dict(request, Oob=False, Retry=True, FailPage=1),
             "leak-retry-stats", "RetryStats")

    bbt = dict(Blocks=4, FactoryBad={2}, ScanError=0, ReadError=False,
               PreserveMarker=True, NoOobBbm=False)
    for no_oob in (False, True):
        for bad in ({1}, {2, 3}):
            add("NandBbt", f"integrated-bbt-no-marker{no_oob}-bad{len(bad)}",
                dict(bbt, NoOobBbm=no_oob, FactoryBad=bad))
    add("NandBbt", "integrated-bbt-source-scan-error", dict(bbt, ScanError=1),
        kind="source-contract", expected="NoIncompletePublish")
    add("NandBbt", "integrated-bbt-source-read-error", dict(bbt, ReadError=True),
        kind="source-contract", expected="NoFalseGood")
    add("NandBbt", "integrated-bbt-assumption-marker-lost", dict(bbt, PreserveMarker=False),
        kind="assumption", expected="NoFalseGood")
    mutation("NandBbt", dict(bbt, NoOobBbm=True), "read-marker-only", "NoFalseGood")

    fmt = dict(Pages=2, Steps=3, WriteBatch=1, ReadBatch=3, Randomized=True, ReaderCompatible=True)
    for wb in (1, 2, 3):
        for rb in (1, 2, 3):
            for randomized in (False, True):
                add("NandFormat", f"integrated-format-w{wb}-r{rb}-random{randomized}",
                    dict(fmt, WriteBatch=wb, ReadBatch=rb, Randomized=randomized))
    add("NandFormat", "integrated-format-incompatible-reader", dict(fmt, ReaderCompatible=False))
    mutation("NandFormat", fmt, "stale-seed", "MediaFormat")

    owner = dict(Backend="descriptor", Read=True, Coherent=True, AbortStops=True)
    for backend in ("descriptor", "register", "external-legacy"):
        for read in (False, True):
            for coherent in (False, True):
                add("NandOwnership", f"integrated-owner-{backend}-read{read}-coherent{coherent}",
                    dict(owner, Backend=backend, Read=read, Coherent=coherent))
    mutation("NandOwnership", owner, "missing-barrier", "DescriptorPublication")
    mutation("NandOwnership", owner, "skip-unmap-sync", "DeviceVisibility")
    mutation("NandOwnership", dict(owner, Read=False), "skip-map-sync", "DeviceVisibility")
    mutation("NandOwnership", dict(owner, Backend="external-legacy"),
             "unconditional-mdma-mode", "RegisterOwnership")
    add("NandOwnership", "integrated-owner-assumption-reset-fails", dict(owner, AbortStops=False),
        kind="assumption", expected="NoUnsafeUnmap")

    persist = dict(Version=7, DetectTorn=True, NoOobBbm=False)
    for version in (0, 127, 254, 255):
        for no_oob in (False, True):
            add("NandPersistence", f"integrated-persistence-version{version}-no-marker{no_oob}",
                dict(persist, Version=version, NoOobBbm=no_oob))
    add("NandPersistence", "integrated-persistence-assumption-undetected-tear",
        dict(persist, DetectTorn=False), kind="assumption", expected="RejectTorn")
    mutation("NandPersistence", dict(persist, Version=255), "unsigned-version", "NewestAvailable")

    rt = dict(FairIrq=True)
    add("NandRt", "integrated-rt-runnable-irq", rt)
    mutation("NandRt", rt, "skip-synchronize", "DrainBeforeReuse")
    add("NandRt", "integrated-rt-assumption-starved-irq", dict(rt, FairIrq=False),
        kind="assumption", expected="Terminates", temporal=True)

    for limit in (1, 2, 4):
        add("NandProgram", f"integrated-program-limit{limit}",
            dict(Limit=limit, RespectLimit=True))
    add("NandProgram", "integrated-program-assumption-no-device-budget",
        dict(Limit=1, RespectLimit=False), kind="assumption", expected="ProgramBudget")
    mutation("NandProgram", dict(Limit=1, RespectLimit=True),
             "retry-program", "NoImplicitReplay")
