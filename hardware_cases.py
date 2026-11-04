# SPDX-License-Identifier: GPL-2.0-or-later
"""Manual-backed register contracts, with independent event-order envelopes."""


def extend_hardware(modules, add, mutation):
    modules["NandRegisterProtocol"] = dict(
        invariants=["FreshCompletion", "CompletedBeforeReuse", "ReadyBeforeCommand",
                    "RbLatchPreserved", "AcceptedExactly"],
        actions=["Clear", "Enqueue", "Latch", "Arm", "Ready", "CommandDone",
                 "DmaDone", "Observe", "Disarm", "Retire"])
    base = dict(Operations=2, DMA=True, WaitRB=True, Polling=False, Order="either", StartClears=True)
    for dma in (False, True):
        for ready in (False, True):
            for polling in (False, True):
                for order in ("either", "cmd-first", "dma-first") if dma else ("either",):
                    add("NandRegisterProtocol", f"register-dma{dma}-rb{ready}-poll{polling}-{order}",
                        dict(base, DMA=dma, WaitRB=ready, Polling=polling, Order=order))
    # Older manuals are incomplete. Check either auto-clear behavior, without
    # presenting either alternative as a measured A10/A23 silicon fact.
    for soc in ("a10", "a23"):
        for clears in (False, True):
            for polling in (False, True):
                add("NandRegisterProtocol", f"register-{soc}-autoclear{clears}-poll{polling}",
                    dict(base, StartClears=clears, Polling=polling, DMA=soc == "a23"))
    mutation("NandRegisterProtocol", base, "only-autoclear", "FreshCompletion")
    mutation("NandRegisterProtocol", base, "fifo-is-completion", "CompletedBeforeReuse")
    mutation("NandRegisterProtocol", base, "command-is-dma", "CompletedBeforeReuse")
    mutation("NandRegisterProtocol", base, "mask-clears-status", "RbLatchPreserved")
