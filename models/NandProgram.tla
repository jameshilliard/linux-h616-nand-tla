---------------------------- MODULE NandProgram ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Finite media contract, NOT a claim that sunxi enforces a chip's program
\* budget. A program stores extra zero bits; erase alone restores ones.
\* RespectLimit is a NAND-part/caller obligation; failed programs consume
\* budget too. No automatic retry of an ambiguously completed program.
EXTENDS Integers, FiniteSets
CONSTANTS Limit, RespectLimit, Mutation
VARIABLE s
vars == <<s>>
Pages == 1..2
Empty == [p \in Pages |-> {}]
Init == s = [pc |-> "choose", image |-> Empty, before |-> Empty,
    count |-> [p \in Pages |-> 0], page |-> 1, request |-> {},
    observed |-> {}, ordinal |-> 0, epoch |-> 0, failed |-> FALSE,
    replayed |-> FALSE, result |-> "unset", last |-> "init"]
Choose ==
    /\ s.pc = "choose" /\ s.ordinal < 2
    /\ \E p \in Pages, data \in {{1}, {2,3}, {1,2,4}} :
       s' = [s EXCEPT !.page = p, !.request = data, !.pc = "admit",
                      !.failed = FALSE, !.result = "unset"]
Admit ==
    /\ s.pc = "admit"
    /\ ~RespectLimit \/ s.count[s.page] < Limit
    /\ s' = [s EXCEPT !.pc = "program"]
Reject ==
    /\ s.pc = "admit" /\ RespectLimit /\ s.count[s.page] >= Limit
    /\ s' = [s EXCEPT !.result = "rejected", !.ordinal = @ + 1, !.pc = "choose"]
Program ==
    /\ s.pc = "program"
    /\ s' = [s EXCEPT !.before = s.image, !.image[s.page] = @ \cup s.request,
        !.count[s.page] = @ + 1, !.last = "program", !.result = "success", !.pc = "read"]
Tear ==
    /\ s.pc = "program"
    /\ \E bits \in SUBSET s.request :
       s' = [s EXCEPT !.before = s.image, !.image[s.page] = @ \cup bits,
         !.count[s.page] = @ + 1, !.last = "program", !.failed = TRUE,
         !.replayed = Mutation = "retry-program", !.result = "error",
         !.pc = IF Mutation = "retry-program" THEN "program" ELSE "read"]
Read ==
    /\ s.pc = "read"
    /\ s' = [s EXCEPT !.observed = s.image[s.page], !.ordinal = @ + 1, !.pc = "choose"]
Erase ==
    /\ s.pc = "choose" /\ s.ordinal = 2 /\ s.epoch = 0
    /\ s' = [s EXCEPT !.before = s.image, !.image = Empty,
         !.count = [p \in Pages |-> 0], !.last = "erase", !.ordinal = 0,
         !.epoch = 1, !.result = "unset"]
Finish ==
    /\ s.pc = "choose" /\ s.ordinal = 2 /\ s.epoch = 1
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Choose \/ Admit \/ Reject \/ Program \/ Tear \/ Read \/ Erase \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ProgramBudget == \A p \in Pages : s.count[p] <= Limit
NoResurrection == s.last = "program" =>
    \A p \in Pages : s.before[p] \subseteq s.image[p]
Readback == s.pc \in {"choose", "done"} /\ s.result = "success" =>
    s.observed = s.image[s.page] /\ s.request \subseteq s.observed
NoImplicitReplay == ~s.replayed
Terminates == <> (s.pc = "done")
=============================================================================
