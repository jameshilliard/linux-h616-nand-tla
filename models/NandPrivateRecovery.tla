---------------------- MODULE NandPrivateRecovery ----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Candidate H616 DMA timeout path, private allocations and final release.
\* BusRetires/BusClears are independent hardware assumptions, not observations
\* available to the driver. A successful API result does NOT prove them.
\* PIO timeout recovery and interrupted NAND programming are separate models.
EXTENDS Integers, Sequences
CONSTANTS Direction, Fault, SoftRetires, BusRetires, BusClears, NextChip, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "launch", request |-> 1, allocated |-> TRUE,
    active |-> FALSE, unsafe |-> FALSE, closed |-> FALSE, held |-> FALSE,
    irqMasked |-> FALSE, drained |-> FALSE, dirty |-> FALSE, context |-> 1,
    copied |-> FALSE, callerReturned |-> FALSE, retained |-> FALSE,
    overlap |-> FALSE, badCopy |-> FALSE, staleContext |-> FALSE,
    useAfterFree |-> FALSE, failedOpen |-> FALSE, writeReplay |-> FALSE]
Launch == /\ s.pc = "launch"
    /\ s' = [s EXCEPT !.active = TRUE, !.pc = "timeout"]
Timeout == /\ s.pc = "timeout"
    /\ s' = [s EXCEPT !.irqMasked = TRUE, !.drained = TRUE, !.pc = "soft"]
SoftReset == /\ s.pc = "soft"
    /\ s' = [s EXCEPT !.active = IF SoftRetires THEN FALSE ELSE @,
        !.dirty = TRUE, !.context = 0, !.unsafe = TRUE, !.pc = "assert"]
AssertReset == /\ s.pc = "assert"
    /\ s' = IF Fault \in {"missing", "assert"} THEN
        [s EXCEPT !.closed = TRUE, !.pc = "return"]
        ELSE [s EXCEPT !.held = TRUE,
            !.active = IF BusRetires THEN FALSE ELSE @,
            !.unsafe = FALSE, !.pc = "deassert"]
DeassertReset == /\ s.pc = "deassert"
    /\ s' = IF Fault = "deassert" THEN
        [s EXCEPT !.closed = TRUE, !.pc = "return"]
        ELSE [s EXCEPT !.held = FALSE, !.dirty = ~BusClears,
            !.irqMasked = FALSE, !.pc = "return"]
Return == /\ s.pc = "return"
    /\ s' = [s EXCEPT !.callerReturned = TRUE,
        !.badCopy = @ \/ (Mutation = "copy-timeout" /\ Direction = "read"),
        !.pc = IF s.closed THEN "next" ELSE "retry"]
Retry == /\ s.pc = "retry"
    /\ s' = IF Direction = "write" /\ Mutation # "replay-write" THEN
        [s EXCEPT !.pc = "next"]
        ELSE [s EXCEPT !.overlap = @ \/ s.active,
            !.writeReplay = @ \/ Direction = "write",
            !.context = IF Mutation = "skip-context" THEN @ ELSE 1,
            !.pc = "pio"]
Pio == /\ s.pc = "pio"
    /\ s' = [s EXCEPT !.badCopy = @ \/ s.dirty,
        !.staleContext = @ \/ s.context # 1 \/ s.held,
        !.copied = TRUE, !.dirty = FALSE, !.pc = "next"]
NextRequest == /\ s.pc = "next"
    /\ s' = IF s.closed /\ Mutation # "reopen" THEN
        [s EXCEPT !.pc = "release"]
        ELSE [s EXCEPT !.failedOpen = @ \/ s.closed,
            !.overlap = @ \/ s.active,
            !.context = NextChip, !.request = 2, !.pc = "complete"]
Complete == /\ s.pc = "complete"
    /\ s' = [s EXCEPT !.badCopy = @ \/ (Direction = "read" /\ s.dirty),
        !.staleContext = @ \/ s.context # NextChip \/ s.held,
        !.active = FALSE, !.pc = "release"]
Release == /\ s.pc = "release"
    /\ LET keep == s.unsafe /\ Mutation # "free-unsafe"
       IN s' = [s EXCEPT !.allocated = keep, !.retained = keep,
           !.useAfterFree = @ \/ (~keep /\ s.active), !.pc = "done"]
\* A delayed DMA beat may happen after the failed call returned. Its only
\* target must still be the retained private allocation, never caller memory.
LateBeat == /\ s.active
    /\ s' = [s EXCEPT !.useAfterFree = @ \/ ~s.allocated \/
        (Mutation = "direct-caller-dma" /\ s.callerReturned)]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Launch \/ Timeout \/ SoftReset \/ AssertReset \/ DeassertReset \/
        Return \/ Retry \/ Pio \/ NextRequest \/ Complete \/ Release \/ LateBeat \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
NoFreedDMATarget == ~s.useAfterFree
NoActiveReuse == ~s.overlap
SoundRead == ~s.badCopy
ContextRestored == ~s.staleContext
FailedRecoveryClosed == ~s.failedOpen
NoWriteReplay == ~s.writeReplay
ResetSerialized == s.held => s.irqMasked /\ s.drained
Retention == s.pc = "done" /\ s.unsafe /\ Mutation # "free-unsafe" => s.retained
Terminates == <> (s.pc = "done")
=============================================================================
