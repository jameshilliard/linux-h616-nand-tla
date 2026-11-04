-------------------------- MODULE NandRelocation --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Bounded write_bbt relocation + two-copy update + restart. Physical program
\* failures may destroy a destination; a recognizable torn header is not a
\* commit record. ECC rejection of tears remains an explicit assumption.
EXTENDS Integers, FiniteSets
CONSTANTS EraseFails, ProgramFails, DetectTorn, Mutation
VARIABLE s
vars == <<s>>
Old == {1}
New == {1, 2}
Empty == [valid |-> FALSE, version |-> 0, data |-> {}, torn |-> FALSE]
Initial == [valid |-> TRUE, version |-> 0, data |-> Old, torn |-> FALSE]
Init == s = [pc |-> "select", copies |-> [i \in 1..4 |-> IF i <= 2 THEN Initial ELSE Empty],
    pointers |-> <<1, 2>>, which |-> 1, target |-> 0, bad |-> {},
    acknowledged |-> FALSE, recovered |-> {}, chosen |-> 0, result |-> "unset"]
Other == s.pointers[3 - s.which]
Candidates == ((1..4) \ s.bad) \ (IF Mutation = "overwrite-mirror" THEN {} ELSE {Other})
Select ==
    /\ s.pc = "select"
    /\ s' = IF Candidates = {} THEN [s EXCEPT !.pc = "recover", !.result = "nospace"] ELSE
        [s EXCEPT !.target = CHOOSE i \in Candidates : \A j \in Candidates : i <= j,
                  !.pc = "erase"]
Erase ==
    /\ s.pc = "erase"
    /\ s' = [s EXCEPT !.copies[s.target] = Empty,
        !.bad = IF s.target \in EraseFails THEN @ \cup {s.target} ELSE @,
        !.pc = IF s.target \in EraseFails THEN "select" ELSE "program"]
Program ==
    /\ s.pc = "program"
    /\ s' = IF s.target \in ProgramFails THEN
        [s EXCEPT !.bad = @ \cup {s.target}, !.pc = "select"] ELSE
        [s EXCEPT !.copies[s.target] = [valid |-> TRUE, version |-> 1, data |-> New, torn |-> FALSE],
            !.pointers[s.which] = s.target, !.pc = "advance"]
Tear ==
    /\ s.pc = "program"
    /\ s' = [s EXCEPT !.copies[s.target] =
        [valid |-> ~DetectTorn, version |-> 1, data |-> {2}, torn |-> TRUE], !.pc = "recover"]
Advance ==
    /\ s.pc = "advance"
    /\ s' = IF s.which = 1 THEN [s EXCEPT !.which = 2, !.pc = "select"] ELSE
        [s EXCEPT !.acknowledged = TRUE, !.result = "ok", !.pc = "recover"]
Cut ==
    /\ s.pc \in {"select", "erase", "advance"}
    /\ s' = [s EXCEPT !.pc = "recover", !.result = "cut"]
Recover ==
    /\ s.pc = "recover"
    /\ LET valid == {i \in 1..4 : s.copies[i].valid}
           chosen == IF valid = {} THEN 0 ELSE CHOOSE i \in valid :
               \A j \in valid : s.copies[i].version >= s.copies[j].version
       IN s' = [s EXCEPT !.chosen = chosen,
           !.recovered = IF chosen = 0 THEN {} ELSE s.copies[chosen].data, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Select \/ Erase \/ Program \/ Tear \/ Advance \/ Cut \/ Recover \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
RetainOld == s.pc = "done" => Old \subseteq s.recovered
AckDurable == s.pc = "done" /\ s.acknowledged => New \subseteq s.recovered
RejectTorn == s.pc = "done" /\ s.chosen # 0 => ~s.copies[s.chosen].torn
SeparateCopies == s.acknowledged => s.pointers[1] # s.pointers[2]
Terminates == <> (s.pc = "done")
=============================================================================
