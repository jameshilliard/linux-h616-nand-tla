# SPDX-License-Identifier: GPL-2.0-or-later
"""Cross-boundary tests; source/assumption witnesses are not passing proofs."""


def extend_boundaries(modules, add, mutation):
    modules.update({
        "NandSequence": dict(invariants=["CurrentContext", "NoInheritedEnable", "CompletedPrefix"],
            actions=["Select", "Configure", "Transfer", "Cleanup", "Finish"]),
        "NandMtdLifetime": dict(invariants=["ReferenceBalance", "ReleaseAtZero", "ResourcesWhileActive"],
            actions=["Open", "Begin", "FinishIO", "Close", "Unregister", "Cleanup", "Finish"]),
        "NandProvider": dict(invariants=["SafeUnmap", "OneCompletion"],
            actions=["BusDrain", "Command", "ProviderIRQ", "CallbackStart", "CallbackFinish", "WaitDone", "Timeout", "Stop", "Unmap"]),
        "NandDiscovery": dict(invariants=["HardwareHasGeometry", "VendorRequiresHardware", "NoPublicationOnBadCrc"],
            actions=["Read", "Reject", "Decode", "Attach", "Publish"]),
        "NandProviderBanks": dict(invariants=["CorrectRecipient", "CompleteCoverage"],
            actions=["Dispatch"]),
    })
    for kinds in (("ecc", "raw", "ecc"), ("ecc", "status", "ecc"), ("raw", "ecc", "raw")):
        ops = tuple(dict(kind=k, chip=i % 2, page=i + 1, column=i * 1024,
                         spare=(i % 2 + 1) * 2048) for i, k in enumerate(kinds))
        base = dict(Operations=ops, FailAt=0)
        label = "boundary-sequence-" + "-".join(kinds)
        for failure in range(4):
            add("NandSequence", label + f"-fail{failure}", dict(base, FailAt=failure))
    mutation("NandSequence", base, "stale-seed", "CurrentContext")
    mutation("NandSequence", base, "stale-spare", "CurrentContext")
    mutation("NandSequence", base, "dirty-error", "NoInheritedEnable", FailAt=2)
    for users in (0, 1, 2):
        add("NandMtdLifetime", f"boundary-lifetime-excluded-{users}",
            dict(Users=users, ExcludeUnbindWithUsers=True))
        if users:
            add("NandMtdLifetime", f"boundary-lifetime-source-{users}",
                dict(Users=users, ExcludeUnbindWithUsers=False), kind="source-contract",
                expected="ResourcesWhileActive")
    base = dict(CommandOrdersMemory=True, TerminationOrdersMemory=True,
                Synchronize=True, CallbackUsesMapping=True)
    add("NandProvider", "boundary-provider-drained", base)
    add("NandProvider", "boundary-provider-nand-no-client-callback",
        dict(base, Synchronize=False, CallbackUsesMapping=False))
    for label, changes in (("early-command", dict(CommandOrdersMemory=False)),
                           ("posted-stop", dict(TerminationOrdersMemory=False)),
                           ("running-callback", dict(Synchronize=False))):
        add("NandProvider", "boundary-provider-" + label, dict(base, **changes),
            kind="assumption", expected="SafeUnmap")
    base = dict(Page=2048, Oob=256, Step=1024, Strength=32, Vendor=True,
                Engine="host", CrcCopies={3})
    for page in (512, 2048, 8192):
        for vendor in (False, True):
            for crc in (set(), {1}, {3}):
                add("NandDiscovery", f"boundary-discovery-{page}-{vendor}-{sorted(crc)}",
                    dict(base, Page=page, Vendor=vendor, CrcCopies=crc))
    for engine in ("host", "software", "none"):
        add("NandDiscovery", "boundary-discovery-engine-" + engine, dict(base, Engine=engine))
    mutation("NandDiscovery", base, "zero-step-hardware", "HardwareHasGeometry", Page=512)
    for channels in (8,12,16):
        add("NandProviderBanks", f"boundary-banks-source-{channels}",
            dict(Channels=channels, SourceDispatch=True),
            **({} if channels==8 else dict(kind="source-contract",expected="CorrectRecipient")))
        if channels>8:
            add("NandProviderBanks", f"boundary-banks-reference-{channels}",
                dict(Channels=channels, SourceDispatch=False))
