# SPDX-License-Identifier: GPL-2.0-or-later
"""Finite configurations, regression mutations, and explicit assumption tests."""

CASES = []
MODULES = {
    "NandBatch": {
        "invariants": ["Geometry", "RegisterBounds", "LengthRegisters",
                       "CorrectColumns", "LogicalLength", "ExactlyOnce", "Complete"],
        "actions": ["Plan", "Consume", "Advance"],
    },
    "NandOob": {
        "invariants": ["Partition", "ProtectedPreserved", "ReadBounds", "Representation"],
        "actions": ["Read", "BeginConvert", "ConvertParity", "ConvertTail"],
    },
    "NandIrq": {
        "invariants": ["NoStaleRegisterWrites", "NoStaleCompletion", "SuccessHasEvents",
                       "DrainedEventsNotLost", "ReturnedClean"],
        "actions": ["Begin", "Arm", "Arrive", "IrqReadStatus", "IrqReadMask",
                    "IrqAck", "IrqMask", "IrqSignal", "Wake", "Timeout",
                    "PollReady", "Drain", "Recheck", "Cleanup", "Clear", "Enable", "Return"],
    },
    "NandTransfer": {
        "invariants": ["NoUnsafeUnmap", "NoWriteReplay", "OneConfirm",
                       "NoPrematureConfirm", "ReturnCleanup", "ReadStats", "WriteSuccess"],
        "actions": ["Setup", "ReadBegin", "ReadBeginError", "Prepare", "PrepareError",
                    "Begin", "BeginError", "Transfer", "FinishTransfer", "TransferError",
                    "Abort", "Cleanup", "Consume", "ConsumeError", "Tail", "TailError",
                    "Fallback", "Error", "Confirm"],
    },
    "NandEcc": {
        "invariants": ["CaptureBeforeRaw", "ResultCorrect", "StatsCorrect"],
        "actions": ["BeginBatch", "Good", "StartRaw", "Raw", "Advance", "Finish"],
    },
}


def add(module, name, constants, *, expected=None, kind="positive", temporal=False):
    config = dict(constants)
    config.setdefault("Mutation", "none")
    CASES.append(dict(module=module, name=name, constants=config,
                      expected=expected, kind=kind, temporal=temporal))


def mutation(module, base, mutation_name, invariant, **changes):
    constants = dict(base, Mutation=mutation_name, **changes)
    add(module, f"{module}-{mutation_name}", constants,
        expected=invariant, kind="mutation")


geometries = {
    "legacy-16": (4,) * 16,
    "h616-packed-2": (8, 0),
    "h616-packed-32": (16,) + (0,) * 31,
    "h616-greedy-32": (32,) * 16 + (12, 0) + (0,) * 14,
    "uneven": (32, 16, 4, 0, 0),
}
for name, user in geometries.items():
    for dma in (False, True):
        c = dict(User=user, Parity=56, StepSize=1024,
                 OobSize=len(user) * 56 + sum(user) + 13,
                 MaxSteps=16 if name.startswith("legacy") else 32,
                 VariableLength=not name.startswith("legacy"), DMA=dma,
                 Start=0, Stop=len(user))
        add("NandBatch", f"batch-{name}-{'dma' if dma else 'pio'}", c)
        if name == "h616-greedy-32" and dma:
            mutation("NandBatch", c, "unbounded-bank", "RegisterBounds")
            mutation("NandBatch", c, "late-multiple-slots", "CorrectColumns")
            mutation("NandBatch", c, "lost-spare-base", "CorrectColumns")
        if name == "uneven" and not dma:
            mutation("NandBatch", c, "pio-slot-length", "LogicalLength")
            add("NandBatch", "batch-subpage-pio", dict(c, Start=1, Stop=4))
        if name == "h616-packed-2" and not dma:
            mutation("NandBatch", c, "duplicate-step", "ExactlyOnce")
            add("NandBatch", "batch-small-page-pio", dict(
                c, User=(4,), StepSize=512, OobSize=64, Stop=1, VariableLength=False))

for name in ("legacy-16", "h616-packed-2", "h616-packed-32", "uneven"):
    user = geometries[name]
    for dma in (False, True):
        c = dict(User=user, Parity=56, OobSize=len(user) * 56 + sum(user) + 7, DMA=dma)
        add("NandOob", f"oob-{name}-{'dma' if dma else 'pio'}", c)
        if name == "uneven" and not dma:
            mutation("NandOob", c, "merge-protected", "ProtectedPreserved")
            mutation("NandOob", c, "wrong-tail-seed", "Representation")
            add("NandOob", "oob-no-tail", dict(c, OobSize=len(user) * 56 + sum(user)))

irq = dict(Events={"cmd", "dma"}, Operations=2, Polling=False)
add("NandIrq", "irq-command-and-dma", irq)
add("NandIrq", "irq-command-only", dict(irq, Events={"cmd"}))
add("NandIrq", "poll-command-only", dict(irq, Events={"cmd"}, Polling=True))
mutation("NandIrq", irq, "complete-before-registers", "NoStaleRegisterWrites")
mutation("NandIrq", irq, "no-drain", "NoStaleRegisterWrites")
mutation("NandIrq", irq, "recheck-original-mask", "DrainedEventsNotLost")

for write in (False, True):
    c = dict(Write=write, Batches=3, AbortStops=True)
    add("NandTransfer", "transfer-write" if write else "transfer-read", c)
    if write:
        for name, invariant in (
            ("partial-write-retry", "NoWriteReplay"),
            ("oob-double-confirm", "OneConfirm"),
            ("confirm-on-error", "NoPrematureConfirm"),
            ("leak-ecc", "ReturnCleanup"),
        ):
            mutation("NandTransfer", c, name, invariant)
    else:
        mutation("NandTransfer", c, "keep-partial-stats", "ReadStats")
        add("NandTransfer", "assumption-abort-does-not-stop-dma",
            dict(c, AbortStops=False), expected="NoUnsafeUnmap", kind="assumption")

kinds = {"normal", "zero", "ones", "error", "zero-error", "ones-error"}
for vendor in (False, True):
    for batch in (1, 3):
        for oob in (False, True):
            for result in ("bad", "erased", "io") if not vendor else ("bad", "erased"):
                c = dict(N=3, Batch=batch, Vendor=vendor, OobRequired=oob,
                         RawResult=result, SpareErased=result == "erased", Kinds=kinds)
                add("NandEcc",
                    f"ecc-{'vendor' if vendor else 'generic'}-batch{batch}-oob{int(oob)}-{result}", c)
                if not vendor and batch == 3 and oob and result == "bad":
                    mutation("NandEcc", c, "whole-pattern-word", "ResultCorrect")
                    mutation("NandEcc", c, "overwrite-pattern", "ResultCorrect")
                if vendor and batch == 1 and oob and result == "erased":
                    mutation("NandEcc", c, "spare-before-zero", "ResultCorrect")
counts = dict(N=6, Batch=5, Vendor=False, OobRequired=True,
              RawResult="bad", SpareErased=False, Kinds={"normal"})
add("NandEcc", "ecc-counter-word-boundary", counts)
mutation("NandEcc", dict(counts, Batch=1), "stale-count-cache", "StatsCorrect")
add("NandEcc", "ecc-mixed-raw-errors", dict(
    N=3, Batch=3, Vendor=False, OobRequired=True,
    RawResult="mixed", SpareErased=False, Kinds=kinds))

from extended_cases import extend
extend(MODULES, add, mutation)

from integration_cases import extend_integration
extend_integration(MODULES, add, mutation)

from validation_cases import extend_validation
extend_validation(MODULES, add, mutation)

from boundary_cases import extend_boundaries
extend_boundaries(MODULES, add, mutation)

from next_cases import extend_next
extend_next(MODULES, add, mutation)

from hardware_cases import extend_hardware
extend_hardware(MODULES, add, mutation)

from contract_cases import extend_contracts
extend_contracts(MODULES, add, mutation)
