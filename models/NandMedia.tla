------------------------------ MODULE NandMedia ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Bit sets represent bytes. Programming is old AND new, never overwriting 0
\* with 1. The reversible seed mask abstracts scrambling, NOT BCH correction.
\* Addresses 1/4/5/6 represent BBM/protected-OOB/parity/tail respectively.
EXTENDS Integers, FiniteSets
CONSTANTS Pages, Cells, Requested, Scramble, Mutation
VARIABLE s
vars == <<s>>
Bits == 0..7
Byte(n) == {b \in Bits : (n \div 2^b) % 2 = 1}
Xor(a,b) == (a \ b) \cup (b \ a)
Payload(p,i) == Byte(32*p + 3*i)
Mask(p,i) == IF Scramble /\ i # 1 THEN Byte(17*p + 5*i) ELSE {}
Encoded(p,i) == Xor(Payload(p,i), Mask(p,i))
Init == s = [pc |-> "program", page |-> 1,
    media |-> [p \in 1..Pages |-> [i \in 1..Cells |-> Bits]],
    expected |-> [p \in 1..Pages |-> [i \in 1..Cells |-> Bits]],
    todo |-> Requested, failed |-> FALSE, verified |-> {}, good |-> TRUE]
Program ==
    /\ s.pc = "program" /\ s.todo # {}
    /\ \E i \in s.todo :
        LET target == IF Mutation = "wrong-column" THEN (i % Cells) + 1 ELSE i
            page == IF Mutation = "wrong-page" THEN (s.page % Pages) + 1 ELSE s.page
        IN s' = [s EXCEPT !.media[page][target] = @ \cap Encoded(s.page,i),
            !.expected[s.page][i] = @ \cap Encoded(s.page,i), !.todo = @ \ {i}]
FailProgram ==
    /\ s.pc = "program" /\ s.todo # {}
    /\ \E partial \in SUBSET s.todo :
        s' = [s EXCEPT
            !.media[s.page] = [i \in 1..Cells |-> IF i \in partial
                THEN s.media[s.page][i] \cap Encoded(s.page,i) ELSE s.media[s.page][i]],
            !.expected[s.page] = [i \in 1..Cells |-> IF i \in partial
                THEN s.expected[s.page][i] \cap Encoded(s.page,i) ELSE s.expected[s.page][i]],
            !.failed = Mutation # "false-success", !.pc = "read"]
FinishProgram ==
    /\ s.pc = "program" /\ s.todo = {}
    /\ s' = [s EXCEPT !.pc = "read"]
Read ==
    /\ s.pc = "read"
    /\ s' = [s EXCEPT !.verified = @ \cup {s.page},
        !.good = @ /\ \A i \in Requested :
            IF s.failed THEN s.media[s.page][i] = s.expected[s.page][i]
            ELSE Xor(s.media[s.page][i], Mask(s.page,i)) = Payload(s.page,i),
        !.pc = "advance"]
Advance ==
    /\ s.pc = "advance"
    /\ s' = IF s.page = Pages THEN [s EXCEPT !.pc = "done"] ELSE
        [s EXCEPT !.page = @ + 1, !.pc = "program", !.todo = Requested, !.failed = FALSE]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Program \/ FailProgram \/ FinishProgram \/ Read \/ Advance \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ExactMedia == s.media = s.expected
PreserveUnrequested == \A p \in 1..Pages, i \in (1..Cells) \ Requested : s.media[p][i] = Bits
RoundTrip == s.good
NoFalseSuccess == s.pc = "read" /\ ~s.failed => s.todo = {}
Terminates == <> (s.pc = "done")
=============================================================================
