-------------------------- MODULE NandPersistence --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Two BBT copies. A cut during program may leave a recognizable marker but a
\* torn payload. ECC detection is a contract, not an invented commit bit/CRC.
EXTENDS Integers, FiniteSets
CONSTANTS Version, DetectTorn, NoOobBbm, Mutation
VARIABLE s
vars == <<s>>
Old == {1}
New == {1, 2}
NextVersion == (Version + 1) % 256
Fresh == [valid |-> TRUE, version |-> Version, data |-> Old, torn |-> FALSE]
Init == s = [pc |-> "mark", copies |-> <<Fresh, Fresh>>, which |-> 1,
    marker |-> Old, acknowledged |-> FALSE, cut |-> FALSE, recovered |-> {},
    chosen |-> 0, programs |-> <<1, 1>>, erased |-> {}, result |-> "unset"]
Mark ==
    /\ s.pc = "mark"
    /\ s' = [s EXCEPT !.marker = IF NoOobBbm THEN @ ELSE New, !.pc = "erase"]
Erase ==
    /\ s.pc = "erase"
    /\ s' = [s EXCEPT !.copies[s.which].valid = FALSE, !.programs[s.which] = 0,
                      !.erased = @ \cup {s.which}, !.pc = "program"]
Program ==
    /\ s.pc = "program"
    /\ s' = [s EXCEPT !.copies[s.which] =
        [valid |-> TRUE, version |-> NextVersion, data |-> New, torn |-> FALSE],
        !.programs[s.which] = @ + 1, !.pc = "advance"]
Tear ==
    /\ s.pc = "program" /\ ~s.cut
    /\ s' = [s EXCEPT !.copies[s.which] =
        [valid |-> ~DetectTorn, version |-> NextVersion, data |-> {2}, torn |-> TRUE],
        !.programs[s.which] = @ + 1, !.cut = TRUE, !.pc = "recover"]
Advance ==
    /\ s.pc = "advance"
    /\ s' = IF s.which = 1 THEN [s EXCEPT !.which = 2, !.pc = "erase"] ELSE
        [s EXCEPT !.acknowledged = TRUE, !.pc = "recover"]
Cut ==
    /\ s.pc \in {"mark", "erase", "advance"} /\ ~s.cut
    /\ s' = [s EXCEPT !.cut = TRUE, !.pc = "recover"]
\* Linux uses (int8_t)(primary.version - mirror.version) > 0.
Newer(a,b) == IF Mutation = "unsigned-version" THEN a > b ELSE
    (a - b + 256) % 256 \in 1..127
Recover ==
    /\ s.pc = "recover"
    /\ LET a == s.copies[1]
           b == s.copies[2]
           choice == IF ~a.valid THEN (IF b.valid THEN 2 ELSE 0)
                     ELSE IF ~b.valid \/ a.version = b.version \/ Newer(a.version,b.version)
                     THEN 1 ELSE 2
       IN s' = [s EXCEPT !.chosen = choice,
           !.recovered = IF choice = 0 THEN s.marker ELSE s.copies[choice].data,
           !.pc = "done", !.result = "ok"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Mark \/ Erase \/ Program \/ Tear \/ Advance \/ Cut \/ Recover \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
RetainOld == s.pc = "done" => Old \subseteq s.recovered
AckDurable == s.pc = "done" /\ s.acknowledged => New \subseteq s.recovered
ProgramLimit == \A i \in 1..2 : s.programs[i] <= 1
RejectTorn == s.pc = "done" /\ s.chosen # 0 => ~s.copies[s.chosen].torn
NewestAvailable == s.pc = "done" /\ (\E i \in 1..2 :
    s.copies[i].valid /\ ~s.copies[i].torn /\ s.copies[i].version = NextVersion) =>
    New \subseteq s.recovered
Terminates == <> (s.pc = "done")
=============================================================================
