------------------------ MODULE NandRandomizerStream ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Exact byte positions, symbolic keys. Bit-level LFSR is checked separately
\* in source fixtures. Each operation has an independent reference image;
\* matching writer/reader mistakes cannot manufacture a compatible format.
\* ReloadAtCommand is an unknown hardware alternative, not a silicon claim.
EXTENDS Integers, Sequences
CONSTANTS Operations, ReloadAtCommand, Mutation
VARIABLE s
vars == <<s>>
Min(a,b) == IF a < b THEN a ELSE b
Init == s = [pc |-> "begin", op |-> 1, enabled |-> FALSE, seed |-> 0,
    phase |-> 0, pos |-> 0, commands |-> 0, count |-> 0, accepted |-> 0,
    failed |-> FALSE, physical |-> <<>>, output |-> <<>>, badRead |-> FALSE,
    badWrite |-> FALSE, dirty |-> FALSE, completed |-> 0]
Op == Operations[s.op]
Payload(i) == <<s.op, i>>
Key(i) == IF Op.random THEN <<Op.seed, i>> ELSE <<"plain">>
ActualKey(i) == IF s.enabled THEN <<s.seed, s.phase + i>> ELSE <<"plain">>
Begin == /\ s.pc = "begin" /\ s.op <= Len(Operations)
    /\ s' = [s EXCEPT !.pos = 0, !.commands = 0, !.failed = FALSE,
        !.physical = <<>>, !.output = <<>>, !.pc = "configure"]
Configure == /\ s.pc = "configure"
    /\ s' = [s EXCEPT !.seed = IF Mutation = "stale-seed" THEN @ ELSE Op.seed,
        !.phase = IF Mutation = "continue-next-operation" THEN @ ELSE 0,
        !.enabled = IF Op.random THEN TRUE ELSE @,
        !.pc = IF Op.size = 0 THEN "cleanup" ELSE "issue"]
Issue == /\ s.pc = "issue"
    /\ s' = [s EXCEPT !.count = Min(Op.chunk, Op.size - s.pos),
        !.commands = @ + 1,
        !.phase = IF ReloadAtCommand \/ Mutation = "reseed-chunk" THEN 0 ELSE @,
        !.pc = "transfer"]
Transfer == /\ s.pc = "transfer"
    /\ LET failed == s.commands = Op.fail
           accepted == IF failed THEN Min(1, s.count) ELSE s.count
           encoded == [i \in 1..accepted |-> <<Payload(s.pos + i), ActualKey(i)>>]
           mismatch == \E i \in 1..accepted : ActualKey(i) # Key(s.pos + i)
       IN s' = [s EXCEPT !.accepted = accepted, !.failed = failed,
           !.physical = IF Op.read THEN @ ELSE @ \o encoded,
           !.badRead = @ \/ (Op.read /\ ~failed /\ mismatch),
           !.badWrite = @ \/ (~Op.read /\ mismatch),
           !.phase = @ + accepted, !.pc = IF failed THEN "cleanup" ELSE "copy"]
Copy == /\ s.pc = "copy"
    /\ s' = [s EXCEPT !.output = @ \o [i \in 1..s.count |-> Payload(s.pos + i)],
        !.pos = @ + s.count,
        !.pc = IF s.pos + s.count = Op.size THEN "cleanup" ELSE "issue"]
Cleanup == /\ s.pc = "cleanup"
    /\ LET enabled == Mutation = "dirty-error" /\ s.failed /\ s.enabled
       IN s' = [s EXCEPT !.enabled = enabled, !.dirty = @ \/ enabled,
           !.completed = @ + 1, !.op = @ + 1, !.pc = "begin"]
Finish == /\ s.pc = "begin" /\ s.op > Len(Operations)
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Begin \/ Configure \/ Issue \/ Transfer \/ Copy \/ Cleanup \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
PhysicalEncoding == ~s.badWrite
IndependentDecode == ~s.badRead
CleanReturn == ~s.dirty
SuccessfulPrefix == s.pc \notin {"begin", "done"} => Len(s.output) = s.pos
NoCommandsForZero == s.pc = "cleanup" /\ Op.size = 0 => s.commands = 0
NoReplayAfterError == s.pc = "cleanup" /\ s.failed => s.commands = Op.fail
Terminates == <> (s.pc = "done")
=============================================================================
