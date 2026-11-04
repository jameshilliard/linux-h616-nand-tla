---------------------- MODULE NandRecoverySequence ----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Bounded read/retry/reuse protocol. "source" follows the current driver's
\* abort/cleanup/PIO retry shape. "bus-reset" and "quarantine" are candidate
\* policies, NOT implementations claimed to exist in the Linux driver.
\* Reset/retirement effects are independent contracts, not inferred from IRQ
\* draining or the successful finite hardware experiment.
EXTENDS Integers, Sequences
CONSTANTS Backend, Policy, Faults, RecoveryFailure, FailureAt, StopRetires,
          RetireEvents, HiddenFault, BusClears, NextChip, Mutation
VARIABLE s
vars == <<s>>
Internal == Backend \in {"register", "descriptor"}
Chip == IF s.request = 1 THEN 1 ELSE NextChip
TimeoutNow == IF s.retry THEN Faults[s.request] = "double-timeout"
             ELSE Faults[s.request] # "ok"
Fails(stage) == RecoveryFailure = stage /\ s.request = FailureAt
Init == s = [pc |-> "select", request |-> 1, retry |-> FALSE,
    mapped |-> FALSE, producer |-> FALSE, pendingBus |-> FALSE,
    irqMasked |-> TRUE, drained |-> TRUE, oldEvent |-> FALSE,
    event |-> 0, resetHeld |-> FALSE, dirty |-> FALSE,
    clock |-> 1, context |-> 0, failed |-> FALSE, recovered |-> FALSE,
    unsafe |-> FALSE, stale |-> FALSE, overlap |-> FALSE, badData |-> FALSE,
    badContext |-> FALSE, failedOpen |-> FALSE, returned |-> <<>>]
Select == /\ s.pc = "select"
    /\ s' = [s EXCEPT !.pc = "map", !.clock = Chip, !.context = Chip]
Map == /\ s.pc = "map"
    /\ s' = [s EXCEPT !.mapped = Backend # "pio" /\ ~s.retry,
        !.pc = "launch"]
Launch == /\ s.pc = "launch"
    /\ s' = [s EXCEPT !.producer = TRUE, !.pendingBus = s.mapped,
        !.overlap = @ \/ s.producer \/ s.pendingBus,
        !.irqMasked = FALSE, !.drained = FALSE, !.event = 0,
        !.badContext = @ \/ s.context # Chip \/ s.clock # Chip \/ s.resetHeld,
        !.pc = "wait"]
Complete == /\ s.pc = "wait" /\ ~TimeoutNow /\ s.event = 0
    /\ s' = [s EXCEPT !.producer = FALSE, !.pendingBus = FALSE,
        !.event = s.request, !.pc = "consume"]
\* The old event may arrive at ANY stage; masking an IRQ does not delete it.
LateEvent == /\ s.oldEvent
    /\ s' = [s EXCEPT !.oldEvent = FALSE, !.event = -1]
AcceptOld == /\ s.pc = "wait" /\ s.event = -1
    /\ s' = [s EXCEPT !.stale = TRUE, !.pc = "consume"]
Consume == /\ s.pc = "consume"
    /\ s' = [s EXCEPT !.badData = @ \/ s.dirty,
        !.unsafe = @ \/ (s.mapped /\ (s.producer \/ s.pendingBus)),
        !.mapped = FALSE, !.dirty = FALSE,
        !.irqMasked = TRUE, !.drained = TRUE,
        !.returned = Append(@, "success"), !.pc = "next"]
Timeout == /\ s.pc = "wait" /\ TimeoutNow /\ s.event # -1
    /\ s' = [s EXCEPT !.pc = "mask", !.oldEvent = TRUE]
Mask == /\ s.pc = "mask"
    /\ s' = [s EXCEPT !.irqMasked = TRUE, !.pc = "drain"]
Drain == /\ s.pc = "drain"
    /\ s' = [s EXCEPT !.drained = TRUE, !.event = 0,
        \* Current PIO errors return without the DMA abort helper. A timed
        \* out producer is not silently retired just because IRQs drained.
        !.pc = IF Policy="source" /\ (Backend="pio" \/ s.retry)
               THEN "error" ELSE "stop"]
Stop == /\ s.pc = "stop"
    /\ LET effective == StopRetires /\ Backend # "pio" /\ ~s.retry
       IN s' = [s EXCEPT !.producer = IF effective THEN FALSE ELSE @,
           !.pendingBus = IF effective THEN FALSE ELSE @,
           !.oldEvent = IF RetireEvents /\ effective THEN FALSE ELSE @,
           !.event = IF RetireEvents /\ effective THEN 0 ELSE @,
           !.dirty = @ \/ (Internal /\ ~s.retry /\ HiddenFault),
           !.context = IF Internal /\ ~s.retry THEN 0 ELSE @,
           !.failed = ~effective,
           !.pc = IF Policy = "bus-reset" THEN "assert"
                  ELSE IF Policy = "quarantine" THEN "quarantine"
                  ELSE "cleanup"]
AssertReset == /\ s.pc = "assert"
    /\ s' = IF Fails("assert") THEN
        [s EXCEPT !.failed = TRUE, !.pc = "quarantine"]
        ELSE [s EXCEPT !.resetHeld = TRUE, !.context = 0,
            !.producer = IF StopRetires THEN FALSE ELSE @,
            !.pendingBus = IF StopRetires THEN FALSE ELSE @,
            !.pc = "deassert"]
DeassertReset == /\ s.pc = "deassert"
    /\ s' = IF Fails("deassert") THEN
        [s EXCEPT !.failed = TRUE, !.pc = "quarantine"]
        ELSE [s EXCEPT !.resetHeld = FALSE, !.dirty = ~BusClears,
            !.oldEvent = IF RetireEvents THEN FALSE ELSE @,
            !.event = IF RetireEvents THEN 0 ELSE @,
            !.failed = ~StopRetires, !.recovered = TRUE,
            !.pc = IF StopRetires THEN "cleanup" ELSE "quarantine"]
Cleanup == /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT
        !.unsafe = @ \/ (s.mapped /\ (s.producer \/ s.pendingBus)),
        !.mapped = FALSE, !.pc = "clock"]
RestoreClock == /\ s.pc = "clock"
    /\ s' = IF Fails("clock") THEN
        [s EXCEPT !.failed = TRUE, !.pc = "quarantine"]
        ELSE [s EXCEPT !.clock = Chip, !.pc = "context"]
RestoreContext == /\ s.pc = "context"
    /\ s' = IF Fails("context") THEN
        [s EXCEPT !.failed = TRUE, !.pc = "quarantine"]
        ELSE [s EXCEPT
            !.context = IF Mutation = "skip-context" THEN @ ELSE Chip,
            !.failedOpen = @ \/ (Policy # "source" /\ s.failed),
            !.pc = IF s.retry THEN "error" ELSE "retry"]
Retry == /\ s.pc = "retry"
    /\ s' = [s EXCEPT !.retry = TRUE, !.pc = "map"]
Error == /\ s.pc = "error"
    /\ s' = [s EXCEPT !.returned = Append(@, "error"), !.pc = "next"]
NextRequest == /\ s.pc = "next"
    /\ s' = IF s.request = Len(Faults) THEN [s EXCEPT !.pc = "done"]
        ELSE [s EXCEPT !.request = @ + 1, !.retry = FALSE,
            !.failed = FALSE, !.recovered = FALSE, !.pc = "select"]
Quarantine == /\ s.pc = "quarantine"
    /\ s' = IF Mutation = "reopen-failed-reset" THEN
        [s EXCEPT !.failedOpen = TRUE, !.pc = "cleanup"]
        ELSE [s EXCEPT !.pc = "closed"]
Done == s.pc \in {"closed", "done"} /\ UNCHANGED vars
Next == Select \/ Map \/ Launch \/ Complete \/ LateEvent \/ AcceptOld \/
        Consume \/ Timeout \/ Mask \/ Drain \/ Stop \/ AssertReset \/
        DeassertReset \/ Cleanup \/ RestoreClock \/ RestoreContext \/
        Retry \/ Error \/ NextRequest \/ Quarantine \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
MappingSafe == ~s.unsafe
FreshCompletion == ~s.stale
NoActiveReuse == ~s.overlap
SoundSuccess == ~s.badData
ContextRestored == ~s.badContext
FailedRecoveryClosed == ~s.failedOpen
ResetSerialized == s.resetHeld => s.irqMasked /\ s.drained
BoundedRequests == s.request \in 1..Len(Faults) /\ Len(s.returned) <= Len(Faults)
Terminates == <> (s.pc \in {"closed", "done"})
=============================================================================
