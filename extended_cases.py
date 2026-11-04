# SPDX-License-Identifier: GPL-2.0-or-later
"""Composition, backend, media, core, lifetime and timing configurations."""


def extend(modules, add, mutation):
    modules.update({
        "NandController": dict(
            invariants=["NoUnsafeUnmap", "NoStaleIrq", "NoWriteReplay", "ConfirmOnce",
                        "ReadAccounting", "CleanReturn"],
            actions=["Prepare", "PrepareFail", "Issue", "Hardware", "IrqRead", "IrqReadMask", "IrqAck", "IrqMask",
                     "IrqSignal", "Wake", "Poll", "Timeout", "Drain", "Check", "Disarm",
                     "Abort", "Cleanup", "Fallback", "Fail", "Advance", "Finish", "Return"]),
        "NandDma": dict(
            invariants=["ValidSubmission", "DescriptorVisible", "BlockEncoding",
                        "BalancedMaps", "NoUnsafeUnmap", "CleanReturn"],
            actions=["Map", "Validate", "Prepare", "Store", "Propagate", "Barrier",
                     "Submit", "Fetch", "Complete", "Unmap", "Cleanup"]),
        "NandCommand": dict(
            invariants=["AddressCount", "AddressIdentity", "DataIdentity", "Bounds",
                        "ReadAfterReady", "WriteBeforeIssue", "CheckOnlyPure", "StopOnError"],
            actions=["Address", "Prepare", "Stage", "Issue", "Ready", "Complete"]),
        "NandMedia": dict(
            invariants=["ExactMedia", "PreserveUnrequested", "RoundTrip", "NoFalseSuccess"],
            actions=["Program", "FailProgram", "FinishProgram", "Read", "Advance"]),
        "NandCore": dict(
            invariants=["FormatAdmission", "WholePageClassification", "ErrorSemantics",
                        "RetryAccounting", "DecodedMetadata", "FinalAccounting"],
            actions=["Attach", "CoreTail", "Read", "Interpret", "ReadRetry", "RetryReset", "Return"]),
        "NandLifecycle": dict(
            invariants=["NoUseAfterRelease", "CorrectChipContext", "PublishedOwned", "NoLeak"],
            actions=["Acquire", "Publish", "Begin", "Transfer", "IrqStart", "IrqFinish",
                     "Remove", "Unregister", "Release", "Finish"]),
        "NandProbe": dict(
            invariants=["RegisteredScanned", "NoLeaks", "DmaOwnership", "UniqueChipSelects", "DmaConfigured"],
            actions=["Step", "Success", "Remove", "Unregister", "ManualCleanup", "Devres", "Finish"]),
        "NandTiming": dict(
            invariants=["LookupSafe", "MinimalEncoding", "ActualClockSafe", "CheckOnlyPure",
                        "RejectionCorrect"],
            actions=["Lookup", "Clock", "Program"]),
    })
    controller = dict(Operations=2, Batches=2, Write=False, Backend="descriptor",
                      AbortStops=True, CommandDrains=True, PioQuiesces=True,
                      OldCommandQuiesces=True, PollModes=(False, True))
    for backend in ("descriptor", "register", "external"):
        for write in (False, True):
            add("NandController", f"controller-{backend}-{'write' if write else 'read'}",
                dict(controller, Backend=backend, Write=write))
    mutation("NandController", controller, "skip-drain", "NoStaleIrq")
    mutation("NandController", dict(controller, Write=True), "replay-write", "NoWriteReplay")
    mutation("NandController", controller, "keep-stats", "ReadAccounting")
    add("NandController", "controller-assumption-abort-fails", dict(controller, AbortStops=False),
        kind="assumption", expected="NoUnsafeUnmap")
    add("NandController", "controller-assumption-command-before-dma",
        dict(controller, Backend="external", CommandDrains=False),
        kind="assumption", expected="NoUnsafeUnmap")
    add("NandController", "controller-assumption-pio-timeout-still-busy",
        dict(controller, PioQuiesces=False), kind="assumption", expected="CleanReturn")
    add("NandController", "controller-assumption-late-command-event",
        dict(controller, OldCommandQuiesces=False), kind="assumption", expected="NoStaleIrq")

    dma = dict(Backend="descriptor", Length=2048, Address=(0, 32, 0, 0), Chunks=2,
               MapOK=True, PrepOK=True, SubmitOK=True)
    for backend in ("descriptor", "register", "external"):
        for failure in (None, "MapOK", "PrepOK", "SubmitOK"):
            config = dict(dma, Backend=backend)
            if failure:
                config[failure] = False
            add("NandDma", f"backend-{backend}-{failure or 'ok'}", config)
    for name, changes in (("too-long", dict(Length=65536)), ("odd-length", dict(Length=2049)),
                          ("unaligned", dict(Address=(1, 32, 0, 0))), ("max-mask", dict(Chunks=32)),
                          ("high-address", dict(Address=(0, 240, 255, 255)))):
        add("NandDma", "backend-descriptor-" + name, dict(dma, **changes))
    mutation("NandDma", dma, "no-barrier", "DescriptorVisible")
    mutation("NandDma", dma, "count-not-mask", "BlockEncoding")
    mutation("NandDma", dict(dma, Length=65536), "no-validation", "ValidSubmission")
    mutation("NandDma", dict(dma, Backend="external", SubmitOK=False), "leak-ram", "CleanReturn")

    cmd = dict(Size=2051, SRAM=1024, Parser=True, Read=True, ReadyBusy=True,
               CheckOnly=False, FailChunk=0, AddrStart=0, AddrCount=5)
    for name, changes in (
        ("parser-read", {}), ("parser-write", dict(Read=False)),
        ("parser-h616", dict(SRAM=8192)), ("raw-h616", dict(SRAM=8192, Parser=False, Size=8193)),
        ("poll", dict(ReadyBusy=False, Size=63)), ("boundary64", dict(Size=64)),
        ("check-only", dict(CheckOnly=True)), ("second-chunk-error", dict(FailChunk=2))):
        add("NandCommand", "command-" + name, dict(cmd, **changes))
    mutation("NandCommand", cmd, "ignore-offset", "DataIdentity")
    mutation("NandCommand", cmd, "skip-ready", "ReadAfterReady")
    mutation("NandCommand", dict(cmd, Read=False), "skip-stage", "WriteBeforeIssue")
    add("NandCommand", "command-write-ignore-offset",
        dict(cmd, Read=False, Mutation="ignore-offset"), kind="mutation", expected="DataIdentity")
    add("NandCommand", "command-source-split-address-count", dict(cmd, AddrStart=8, AddrCount=2),
        kind="source-contract", expected="AddressCount")

    media = dict(Pages=2, Cells=6, Requested={2, 3, 4, 5}, Scramble=True)
    for name, changes in (("scrambled", {}), ("plain", dict(Scramble=False)),
                          ("subpage", dict(Requested={3, 4})), ("oob-only", dict(Requested={4, 5}))):
        add("NandMedia", "media-" + name, dict(media, **changes))
    mutation("NandMedia", media, "wrong-column", "ExactMedia")
    mutation("NandMedia", media, "wrong-page", "ExactMedia")
    mutation("NandMedia", media, "false-success", "NoFalseSuccess")

    core = dict(Vendor=True, Engine="host", Scramble=True, SmallPage=False,
                Retry=True, Initial=254, CounterMod=256)
    for vendor in (False, True):
        for engine in ("host", "soft", "none", "ondie"):
            add("NandCore", f"core-{'vendor' if vendor else 'generic'}-{engine}",
                dict(core, Vendor=vendor, Engine=engine))
    add("NandCore", "core-small-page-fallback", dict(core, Vendor=False, SmallPage=True))
    add("NandCore", "core-counter-wrap", dict(core, Initial=255))
    for name, invariant in (("partial-vendor", "WholePageClassification"),
                            ("ignore-failed-delta", "ErrorSemantics"),
                            ("leak-retry-stats", "RetryAccounting"),
                            ("erase-bbt-on-error", "DecodedMetadata")):
        mutation("NandCore", core, name, invariant)

    life = dict(FailAt=0, CoreDrains=True, ClockWorks=True)
    for failure in range(9):
        add("NandLifecycle", f"lifecycle-probe-failure-{failure}", dict(life, FailAt=failure))
    mutation("NandLifecycle", life, "free-before-irq", "NoUseAfterRelease")
    mutation("NandLifecycle", life, "stale-geometry", "CorrectChipContext")
    add("NandLifecycle", "lifecycle-assumption-core-does-not-drain", dict(life, CoreDrains=False),
        kind="assumption", expected="NoUseAfterRelease")
    add("NandLifecycle", "lifecycle-assumption-clock-set-fails", dict(life, ClockWorks=False),
        kind="assumption", expected="CorrectChipContext")

    probe = dict(FailAt=0, Backend="external", MaskOK=True, AllocOK=True,
                 Request="ok", ConfigOK=True, ChipSelects=(0, 1))
    for failure in range(20):
        add("NandProbe", f"probe-stage-{failure}", dict(probe, FailAt=failure))
    for backend in ("descriptor", "register"):
        add("NandProbe", "probe-" + backend, dict(probe, Backend=backend))
    for name, changes in (("mask", dict(Backend="descriptor", MaskOK=False)),
                          ("allocation", dict(Backend="descriptor", AllocOK=False)),
                          ("channel", dict(Request="error")), ("defer", dict(Request="defer")),
                          ("duplicate-cs", dict(ChipSelects=(0, 0))),
                          ("invalid-cs", dict(ChipSelects=(0, 8)))):
        add("NandProbe", "probe-" + name, dict(probe, **changes))
    mutation("NandProbe", probe, "leak-scanned-chip", "NoLeaks")
    mutation("NandProbe", probe, "leak-channel", "NoLeaks")
    add("NandProbe", "probe-source-config-failure", dict(probe, ConfigOK=False),
        kind="source-contract", expected="DmaConfigured")

    tables = {"a10-wb": (6, 12, 16, 20), "a10-adl-whr": (7, 15, 23, 31),
              "a10-rhw": (4, 8, 12, 20), "h616-wb": (28, 44, 60, 76),
              "h616-adl-whr": (0, 12, 28, 44), "h616-rhw": (8, 24, 40, 56)}
    for name, table in tables.items():
        timing = dict(Table=table, Duration=table[1]*10000 + 1, Period=10000,
                      RoundedPeriod=11, ReadAccess=20000, CheckOnly=False)
        add("NandTiming", "timing-" + name, timing)
        add("NandTiming", "timing-" + name + "-reject",
            dict(timing, Duration=table[-1]*10000 + 1))
    timing = dict(Table=(6, 12, 16, 20), Duration=120001, Period=10000,
                  RoundedPeriod=11, ReadAccess=20000, CheckOnly=False)
    add("NandTiming", "timing-check-only", dict(timing, CheckOnly=True))
    mutation("NandTiming", timing, "floor-cycles", "LookupSafe")
    add("NandTiming", "timing-assumption-faster-rounded-clock", dict(timing, RoundedPeriod=7),
        kind="assumption", expected="ActualClockSafe")
