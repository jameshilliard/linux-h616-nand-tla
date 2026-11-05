---------------------- MODULE NandStreamingRecovery ----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Streaming caller-buffer policy. The H616 reset's bus retirement and data
\* path clearing are explicit hardware contracts, not DMA-API guarantees.
\* Runtime assertion failure violates the supported H616 provider contract;
\* the error return alone does not make an active caller buffer safe to unmap.
\* The current driver uses reset_control_reset() at probe and recovery. Assert
\* and Deassert below are internal phases of that provider pulse, not separate
\* consumer API calls. The real H616 CCU holds reset for 10 us and returns zero.
\* "assert" also abstracts reset-core rejection before reaching the provider;
\* "deassert" remains a hypothetical weakened-provider assumption check.
EXTENDS Integers
CONSTANTS Direction, Fault, Normal, Launched, BusRetires, BusClears, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "probe", verified |-> FALSE, admitted |-> FALSE,
    mapped |-> FALSE, active |-> FALSE, held |-> FALSE, closed |-> FALSE,
    masked |-> FALSE, drained |-> FALSE, dirty |-> FALSE, context |-> TRUE,
    returned |-> FALSE, aborted |-> FALSE, maps |-> 0, unmaps |-> 0,
    badAccess |-> FALSE, badUnmap |-> FALSE,
    badData |-> FALSE, replay |-> FALSE]
Probe == /\ s.pc = "probe"
    /\ LET verified == Fault \notin {"probe-missing", "probe-assert"}
       IN s' = [s EXCEPT !.verified = verified,
         !.admitted = verified \/ Mutation = "admit-without-reset",
         !.pc = IF verified \/ Mutation = "admit-without-reset" THEN "map" ELSE "return"]
Map == /\ s.pc = "map"
    /\ s' = IF Fault = "map" THEN [s EXCEPT !.pc = "return"]
        ELSE [s EXCEPT !.mapped = TRUE, !.maps = @ + 1, !.pc = "launch"]
Launch == /\ s.pc = "launch"
    /\ s' = [s EXCEPT !.active = Launched,
        !.pc = IF Normal THEN "complete" ELSE "quiesce"]
Complete == /\ s.pc = "complete"
    /\ s' = [s EXCEPT !.active = FALSE, !.pc = "cleanup"]
Quiesce == /\ s.pc = "quiesce"
    /\ s' = [s EXCEPT !.masked = TRUE, !.drained = TRUE,
        !.aborted = TRUE, !.pc = "soft"]
SoftReset == /\ s.pc = "soft"
    /\ s' = [s EXCEPT !.dirty = TRUE, !.context = FALSE,
        !.pc = IF Mutation = "unmap-before-reset" THEN "cleanup" ELSE "assert"]
AssertReset == /\ s.pc = "assert"
    /\ s' = IF Fault = "assert" THEN
           [s EXCEPT !.closed = TRUE, !.pc = "cleanup"]
         ELSE [s EXCEPT !.held = TRUE, !.active = IF BusRetires THEN FALSE ELSE @,
             !.pc = "deassert"]
DeassertReset == /\ s.pc = "deassert"
    /\ s' = IF Fault = "deassert" THEN
        [s EXCEPT !.closed = TRUE, !.pc = "cleanup"]
        ELSE [s EXCEPT !.held = FALSE, !.dirty = ~BusClears,
            !.masked = FALSE, !.pc = "cleanup"]
Cleanup == /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.badUnmap = @ \/ s.active \/ ~s.mapped,
        !.mapped = FALSE, !.unmaps = @ + 1,
        !.badAccess = @ \/ (s.closed /\ Mutation = "cleanup-mmio-held"),
        !.pc = IF s.aborted /\ ~s.closed THEN "retry" ELSE "return"]
Retry == /\ s.pc = "retry"
    /\ s' = IF Direction = "write" /\ Mutation # "replay-write" THEN
        [s EXCEPT !.pc = "return"]
        ELSE [s EXCEPT !.replay = @ \/ Direction = "write",
            !.context = Mutation # "skip-context", !.pc = "pio"]
Pio == /\ s.pc = "pio"
    /\ s' = [s EXCEPT !.badData = @ \/ s.dirty \/ ~s.context \/ s.active,
        !.badAccess = @ \/ s.held, !.pc = "return"]
Return == /\ s.pc = "return"
    /\ s' = [s EXCEPT !.returned = TRUE, !.pc = "next"]
NextRequest == /\ s.pc = "next"
    /\ s' = [s EXCEPT !.badAccess = @ \/ (s.closed /\ Mutation = "reopen"),
        !.pc = "done"]
Done == /\ s.pc = "done" /\ UNCHANGED vars
Progress == Probe \/ Map \/ Launch \/ Complete \/ Quiesce \/ SoftReset \/
    AssertReset \/ DeassertReset \/ Cleanup \/ Retry \/ Pio \/ Return \/ NextRequest
Spec == Init /\ [][Progress \/ Done]_vars /\ WF_vars(Progress)
CallerSafe == s.returned => ~s.active /\ ~s.mapped
MappingSafe == ~s.badUnmap /\ s.unmaps <= s.maps /\ (s.active => s.mapped)
ResetSerialized == s.held => s.masked /\ s.drained
NoMMIOHeld == ~s.badAccess
SoundRead == ~s.badData
NoWriteReplay == ~s.replay
VerifiedAdmission == s.admitted => s.verified
Terminates == <> (s.pc = "done")
=============================================================================
