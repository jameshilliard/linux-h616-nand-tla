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
                       "NoPrematureConfirm", "FifoTimeoutStopsWrite", "ConfirmFailureReturned",
                       "ReturnCleanup", "ReadStats", "WriteSuccess"],
        "actions": ["Setup", "ReadBegin", "ReadBeginError", "Prepare", "PrepareError", "FifoError",
                    "Begin", "BeginError", "Transfer", "FinishTransfer", "TransferError",
                    "Abort", "Cleanup", "Consume", "ConsumeError", "Tail", "TailError",
                    "Fallback", "Error", "Confirm", "ConfirmError"],
    },
    "NandReadRetry": {
        "invariants": ["DiscardBeforeDecision", "NoClosedRetry", "Success", "Error"],
        "actions": ["Open", "OpenError", "Step", "DmaError", "PioError",
                    "Discard", "Retry", "Closed", "Finish"],
    },
    "NandEcc": {
        "invariants": ["CaptureBeforeRaw", "PendingErrors", "VendorEvidence",
                       "TransportCorrect", "TailCorrect", "ResultCorrect", "StatsCorrect",
                       "PatternReadCount"],
        "actions": ["BeginBatch", "OrdinarySlot", "VendorSlot", "StartRaw", "Raw",
                    "Advance", "Finish"],
    },
    "NandSubpage": {
        "invariants": ["StepScope", "Complete", "Stats", "Result"],
        "actions": ["BeginBatch", "Corrected", "StartRaw", "Reread", "Advance", "Finish"],
    },
    "NandSubpageProgram": {
        "invariants": ["DataScope", "OobScope", "DataComplete", "OobComplete"],
        "actions": ["Begin", "Program", "Oob", "Finish"],
    },
    "NandMarkerAdmission": {
        "invariants": ["MarkerRoundTrips", "OrdinaryMarkerProtected", "SoftwareUnchanged",
                       "GeometryPreserved"],
        "actions": ["Attach"],
    },
    "NandMarkerFallback": {
        "invariants": ["MarkerSafe", "HostRejected", "ExplicitChoice"],
        "actions": ["Attach", "SelectEngine", "PlaceParity"],
    },
    "NandPlainMarker": {
        "invariants": ["Source", "Stats", "Traffic", "Io"],
        "actions": ["First", "Tail", "Marker"],
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


for name, oob, mode in (("data-only", False, "none"),
                         ("auto-oob", True, "auto"),
                         ("place-oob", True, "place")):
    add("NandSubpageProgram", "program-" + name,
        dict(Steps=4, First=1, End=3, OobSteps={1, 2},
             OobRequired=oob, OobMode=mode))
mutation("NandSubpageProgram", dict(Steps=4, First=1, End=3,
                                    OobSteps={1, 2}, OobRequired=False,
                                    OobMode="none"),
         "write-page-tail", "DataScope")
mutation("NandSubpageProgram", dict(Steps=4, First=1, End=3,
                                    OobSteps={0, 1, 2}, OobRequired=True,
                                    OobMode="auto"),
         "oob-outside", "OobScope")


add("NandMarkerAdmission", "firmware-marker-admission", {})
for variant in ("no-guard", "within-field", "relocate"):
    mutation("NandMarkerAdmission", {}, variant, "MarkerRoundTrips")
for variant in ("ordinary-no-guard", "ordinary-within-field"):
    mutation("NandMarkerAdmission", {}, variant, "OrdinaryMarkerProtected")
mutation("NandMarkerAdmission", {}, "restrict-software", "SoftwareUnchanged")
add("NandMarkerFallback", "small-page-hamming-fallback", {})
mutation("NandMarkerFallback", {}, "no-guard", "MarkerSafe")
mutation("NandMarkerFallback", {}, "guard-software", "ExplicitChoice")
add("NandPlainMarker", "ordinary-physical-marker", {})
for variant, rule in (("erase-marker", "Source"), ("count-marker", "Stats"),
                      ("trust-pattern", "Source"), ("wrong-step", "Traffic"),
                      ("read-data-only", "Traffic"), ("read-before-tail", "Traffic"),
                      ("ignore-marker-io", "Io"), ("bypass-correction", "Source")):
    mutation("NandPlainMarker", {}, variant, rule)

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
            ("retry-fifo-timeout", "FifoTimeoutStopsWrite"),
            ("ignore-confirm-error", "ConfirmFailureReturned"),
            ("ignore-retry-confirm-error", "ConfirmFailureReturned"),
        ):
            mutation("NandTransfer", c, name, invariant)
    else:
        mutation("NandTransfer", c, "keep-partial-stats", "ReadStats")
        add("NandTransfer", "assumption-abort-does-not-stop-dma",
            dict(c, AbortStops=False), expected="NoUnsafeUnmap", kind="assumption")

for profile, first, deferred in (("full", 1, False), ("range", 2, False), ("deferred", 1, True)):
    c = dict(N=4, First=first, Deferred=deferred)
    add("NandReadRetry", "dma-read-retry-" + profile, c)
    if profile == "full":
        for name, invariant in (("keep-dma-corrected", "DiscardBeforeDecision"),
                                ("keep-dma-failed", "DiscardBeforeDecision"),
                                ("reuse-dma-peak", "Success"),
                                ("retry-closed-read", "NoClosedRetry")):
            mutation("NandReadRetry", c, name, invariant)

kinds = {"normal", "zero", "ones", "error", "zero-error", "ones-error"}
for vendor in (False, True):
    for batch in (1, 3):
        for oob in (False, True):
            for result in ("bad", "erased", "io") if not vendor else ("bad", "erased"):
                c = dict(N=3, Batch=batch, Vendor=vendor, OobRequired=oob,
                         RawResult=result, SpareErased=result == "erased", Kinds=kinds,
                         Scrambling=vendor, Dma=batch > 1)
                add("NandEcc",
                    f"ecc-{'vendor' if vendor else 'generic'}-batch{batch}-oob{int(oob)}-{result}", c)
                if not vendor and batch == 3 and oob and result == "bad":
                    mutation("NandEcc", c, "whole-pattern-word", "ResultCorrect")
                    mutation("NandEcc", c, "overwrite-pattern", "ResultCorrect")
                    mutation("NandEcc", c, "pattern-before-error", "PendingErrors")
                    mutation("NandEcc", c, "force-dma-pattern-fill", "ResultCorrect")
                    add("NandEcc", "ecc-ordinary-scrambled-plain-marker", dict(c, Scrambling=True))
                    mutation("NandEcc", c, "dispatch-on-scrambling", "ResultCorrect", Scrambling=True)
                if vendor and batch == 1 and oob and result == "erased":
                    mutation("NandEcc", c, "spare-before-zero", "ResultCorrect")
                    mutation("NandEcc", c, "lose-zero-on-error", "ResultCorrect")
                    mutation("NandEcc", c, "ordinary-batch-for-vendor", "VendorEvidence")
                    mutation("NandEcc", c, "ordinary-finish-for-vendor", "ResultCorrect")
                    mutation("NandEcc", c, "skip-error-only-patterns", "PatternReadCount")
                    mutation("NandEcc", c, "read-zero-twice", "PatternReadCount")
                    mutation("NandEcc", c, "always-read-pattern-id", "PatternReadCount")
counts = dict(N=6, Batch=5, Vendor=False, OobRequired=True,
              RawResult="bad", SpareErased=False, Kinds={"normal"}, Scrambling=False, Dma=True)
add("NandEcc", "ecc-counter-word-boundary", counts)
mutation("NandEcc", counts, "reuse-wrong-count-word", "StatsCorrect")
mutation("NandEcc", dict(counts, Batch=1), "stale-count-cache", "StatsCorrect")
add("NandEcc", "ecc-mixed-raw-errors", dict(
    N=3, Batch=3, Vendor=False, OobRequired=True,
    RawResult="mixed", SpareErased=False, Kinds=kinds, Scrambling=False, Dma=True))
raw_tail = dict(N=2, Batch=1, Vendor=False, OobRequired=True, RawResult="clean-mixed",
                SpareErased=False, Kinds={"error"}, Scrambling=True, Dma=True)
add("NandEcc", "ecc-clean-erased-and-media-errors", raw_tail)
for variant in ("overwrite-erased-state", "ignore-clean-erased"):
    mutation("NandEcc", raw_tail, variant, "TailCorrect")

# Every nonempty byte range of each page is chosen by TLC, not by a fixture.
# StepBytes = 3 leaves a middle byte as well as both step boundaries.
sub = dict(Status=(-1, 2, 7, 0, -3, 4), StepBytes=3, FirstBatch=5,
           Transport="dma", Firmware=False)
for transport, batch, name in (("dma", 5, "bank-split"), ("dma", 6, "one-bank"),
                               ("pio", 1, "pio")):
    c = dict(sub, Transport=transport, FirstBatch=batch)
    add("NandSubpage", "subpage-" + name, c)
    add("NandSubpage", "subpage-" + name + "-raw-io", dict(c, Status=(0, -2, 3, -1, -3, 4)))
    add("NandSubpage", "subpage-" + name + "-firmware", dict(c, Firmware=True))

# Set the witness status explicitly so the pre-fix failures cannot amount
# merely to extra internal work yielding the same public result.
for status, name in ((-1, "badmsg"), (-2, "raw-io"), (8, "max-bitflips")):
    add("NandSubpage", "subpage-old-prefix-" + name,
        dict(sub, Status=(status, 0, 0), FirstBatch=3, Mutation="account-prefix"),
        expected="Result", kind="mutation")
for bug, invariant in (("skip-first-pass-only", "StepScope"),
                       ("renumber-slots", "Stats"), ("round-end-down", "Complete"),
                       ("firmware-request-only", "Complete")):
    add("NandSubpage", "subpage-" + bug,
        dict(sub, Firmware=bug == "firmware-request-only", Mutation=bug),
        expected=invariant, kind="mutation")

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

from probe_cases import extend_probe
extend_probe(MODULES, add, mutation)

from probe_trace_cases import extend_probe_trace
extend_probe_trace(MODULES, add, mutation)

from mtd_cases import extend_mtd
extend_mtd(MODULES, add, mutation)

from measured_extension_cases import extend_measured
extend_measured(MODULES, add, mutation)

from post_abort_cases import extend_post_abort
extend_post_abort(MODULES, add, mutation)

from recovery_expansion_cases import extend_recovery_expansion
extend_recovery_expansion(MODULES, add, mutation)

from bbm_init_cases import extend_bbm_init
extend_bbm_init(MODULES, add, mutation)

from deferred_init_cases import extend_deferred_init
extend_deferred_init(MODULES, add, mutation)

from randomizer_cases import extend_randomizer
extend_randomizer(MODULES, add, mutation)

from mtdchar_cases import MODEL as MTDCHAR_MODEL, cases as mtdchar_cases
MODULES["NandMtdCharChunks"] = MTDCHAR_MODEL
CASES.extend(mtdchar_cases())

from private_recovery_cases import extend_private_recovery
extend_private_recovery(MODULES, add, mutation)

from streaming_recovery_cases import extend_streaming_recovery
extend_streaming_recovery(MODULES, add, mutation)

from timing_cases import extend_timing
extend_timing(MODULES, add, mutation)
