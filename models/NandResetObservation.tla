------------------------ MODULE NandResetObservation ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Evidence bookkeeping, NOT an implementation of the controller internals.
\* Encodes the observed envelope: retained buffers quiet through 100 ms;
\* after an active abort, the first read may be wrong, second read was good.
\* The latter is a measured trace bound, not a proven two-read recovery policy.
EXTENDS Integers, Sequences
CONSTANTS Abort, FirstGood, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "active", bufferLive |-> TRUE, resetClear |-> FALSE,
             quiet |-> FALSE, reads |-> <<>>, claimedFresh |-> FALSE,
             evidenceSound |-> TRUE]
Reset == /\ s.pc = "active"
    /\ s' = [s EXCEPT !.pc = "observe", !.resetClear = TRUE]
Observe == /\ s.pc = "observe"
    /\ s' = [s EXCEPT !.pc = "read", !.quiet = TRUE,
        !.bufferLive = Mutation # "free-after-quiet",
        !.claimedFresh = Mutation = "quiet-implies-fresh"]
ReadNext == /\ s.pc = "read" /\ Len(s.reads) < 2
    /\ LET good == IF Len(s.reads) = 0 THEN (~Abort \/ FirstGood) ELSE TRUE
       IN s' = [s EXCEPT !.reads = Append(@, good),
           !.evidenceSound = @ /\ (~s.claimedFresh \/ good),
           !.claimedFresh = FALSE]
Finish == /\ s.pc = "read" /\ Len(s.reads) = 2
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Reset \/ Observe \/ ReadNext \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ProbeRetainsBuffers == s.bufferLive
EvidenceNotOverstated == s.evidenceSound
Bounds == Len(s.reads) <= 2
Terminates == <> (s.pc = "done")
=============================================================================
