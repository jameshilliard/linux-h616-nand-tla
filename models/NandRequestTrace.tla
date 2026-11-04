-------------------------- MODULE NandRequestTrace --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Complete source caller traces. Hardware effects are fixture observations;
\* this independently checks ordering/ownership, not all C executions.
EXTENDS Integers, Sequences
CONSTANT Traces
VARIABLE s
vars == <<s>>
Init == \E t \in 1..Len(Traces):
    s = [t |-> t, i |-> 1, mapped |-> FALSE, active |-> FALSE, pending |-> FALSE,
         inPage |-> FALSE, write |-> FALSE, terminal |-> FALSE, begins |-> 0,
         confirms |-> 0, abortNeeded |-> FALSE, protocol |-> TRUE,
         snapshot |-> TRUE, safe |-> TRUE, clean |-> TRUE, replay |-> TRUE,
         result |-> TRUE, maps |-> 0, unmaps |-> 0]
Trace == Traces[s.t]
Events == Trace.events
Advance ==
    /\ s.i <= Len(Events)
    /\ LET e == Events[s.i]
           k == e[1]
           arg == e[2]
           mapped == IF k = "map" THEN TRUE ELSE IF k = "unmap" THEN FALSE ELSE s.mapped
           active == IF k = "launch" /\ arg = 1 THEN TRUE
                     ELSE IF k \in {"abort", "terminate-ok"} \/ (k = "wait" /\ arg = 0)
                     THEN FALSE ELSE s.active
           pending == IF k = "launch" THEN TRUE ELSE IF k = "wait" THEN FALSE ELSE s.pending
           abortNeeded == IF k = "wait" /\ arg < 0 /\ s.mapped THEN TRUE
                          ELSE IF k = "abort" THEN FALSE ELSE s.abortNeeded
           protocol == CASE k = "begin" -> s.i = 1
             [] k = "page-enter" -> ~s.inPage /\ ~s.mapped /\ ~s.pending
             [] k = "page-exit" -> s.inPage /\ ~s.pending /\ ~s.mapped /\ ~s.abortNeeded
             [] k = "map" -> s.inPage /\ ~s.mapped
             [] k = "prepare" -> (arg = 0 <=> s.mapped)
             [] k = "launch" -> s.inPage /\ ~s.pending /\ ~s.abortNeeded /\ (arg = 1 => s.mapped)
             [] k = "wait" -> s.pending
             [] k = "abort" -> s.mapped
             [] k = "unmap" -> s.mapped /\ ~s.abortNeeded
             [] k = "cleanup" -> ~s.mapped
             [] k = "program-begin" -> s.inPage /\ s.write
             [] k = "confirm" -> s.inPage /\ s.write /\ s.begins = 1 /\ ~s.pending /\ ~s.mapped
             [] k = "result" -> ~s.inPage /\ s.i = Len(Events)
             [] OTHER -> k \in {"terminate-ok", "terminate-error", "wire-error"}
       IN s' = [s EXCEPT !.i = @ + 1, !.mapped = mapped, !.active = active,
           !.pending = pending, !.abortNeeded = abortNeeded,
           !.inPage = IF k = "page-enter" THEN TRUE ELSE IF k = "page-exit" THEN FALSE ELSE @,
           !.write = IF k = "page-enter" THEN arg = 1 ELSE @,
           !.terminal = IF k = "wait" /\ arg < 0 /\ s.write THEN TRUE ELSE @,
           !.begins = IF k = "program-begin" THEN @ + 1 ELSE @,
           !.confirms = IF k = "confirm" THEN @ + 1 ELSE @,
           !.maps = IF k = "map" THEN @ + 1 ELSE @,
           !.unmaps = IF k = "unmap" THEN @ + 1 ELSE @,
           !.protocol = @ /\ protocol,
           !.snapshot = @ /\ (e[4] = 1 <=> mapped) /\ (e[5] = 1 <=> active),
           !.safe = @ /\ e[6] = 0 /\ (k = "unmap" => ~s.active),
           !.clean = @ /\ (k = "wait" => e[7] = 0) /\
                         (k \in {"page-exit", "result"} => e[7] = 0 /\ e[8] = 0),
           !.replay = @ /\ (s.terminal => k \notin {"launch", "program-begin", "confirm"}),
           !.result = @ /\ (k = "result" => arg = Trace.ret /\ e[3] = Trace.retlen /\
               Trace.retlen <= Trace.len /\ Trace.oobretlen <= Trace.ooblen /\
               (arg >= 0 => Trace.retlen = Trace.len /\ Trace.oobretlen = Trace.ooblen))]
Done == s.i > Len(Events) /\ UNCHANGED vars
Next == Advance \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Protocol == s.protocol
Snapshots == s.snapshot
MappingSafety == s.safe
CleanReturn == s.clean
NoWriteReplay == s.replay /\ s.confirms <= 1 /\ s.begins <= 1
Results == s.result
Balanced == s.i > Len(Events) => ~s.mapped /\ ~s.active /\ s.maps = s.unmaps
Terminates == <> (s.i > Len(Events))
=============================================================================
