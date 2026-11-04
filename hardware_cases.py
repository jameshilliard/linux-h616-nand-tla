# SPDX-License-Identifier: GPL-2.0-or-later
"""Manual-backed register contracts, with independent event-order envelopes."""


def extend_hardware(modules, add, mutation):
    modules["NandRegisterProtocol"] = dict(
        invariants=["FreshCompletion", "CompletedBeforeReuse", "ReadyBeforeCommand",
                    "RbLatchPreserved", "AcceptedExactly"],
        actions=["Clear", "Enqueue", "Latch", "Arm", "Ready", "CommandDone",
                 "DmaDone", "Observe", "Disarm", "Retire"])
    base = dict(Operations=2, DMA=True, WaitRB=True, Polling=False, Order="either")
    for dma in (False, True):
        for ready in (False, True):
            for polling in (False, True):
                for order in ("either", "cmd-first", "dma-first") if dma else ("either",):
                    add("NandRegisterProtocol", f"register-dma{dma}-rb{ready}-poll{polling}-{order}",
                        dict(base, DMA=dma, WaitRB=ready, Polling=polling, Order=order))
    mutation("NandRegisterProtocol", base, "only-autoclear", "FreshCompletion")
    mutation("NandRegisterProtocol", base, "fifo-is-completion", "CompletedBeforeReuse")
    mutation("NandRegisterProtocol", base, "command-is-dma", "CompletedBeforeReuse")
    mutation("NandRegisterProtocol", base, "mask-clears-status", "RbLatchPreserved")
