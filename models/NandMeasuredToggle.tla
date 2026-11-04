------------------------- MODULE NandMeasuredToggle -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Measured H616 NORMAL-command behavior, not a claim about another NFC.
\* Four commands: randomization enabled, disabled, re-enabled, still enabled.
\* Symbolic key positions complement the independent bit-level capture replay.
EXTENDS Integers, Sequences
CONSTANTS Chunk, Read, PageMode, Mutation
VARIABLE s
vars == <<s>>
Init == s = [command |-> 1, phase |-> 0, expectedPhase |-> 0,
             contract |-> TRUE, seen |-> <<>>]
Transfer == /\ s.command <= 4
    /\ LET enabled == s.command # 2
           continuous == Read /\ PageMode
           phase == IF continuous THEN
                        IF s.command = 3 /\ Mutation = "reset-on-enable"
                        THEN 0 ELSE s.phase
                    ELSE 0
           expected == IF continuous THEN s.expectedPhase ELSE 0
           bypassTail == Read /\ Chunk % 4 # 0
           keys == [i \in 1..Chunk |->
               IF ~enabled \/ (bypassTail /\ i = Chunk /\ Mutation # "randomize-tail")
               THEN 0 ELSE phase + i]
           observed == [i \in 1..Chunk |->
               IF ~enabled \/ (bypassTail /\ i = Chunk)
               THEN 0 ELSE expected + i]
       IN s' = [s EXCEPT !.command = @ + 1,
            !.phase = IF enabled \/ Mutation = "advance-disabled" THEN phase + Chunk ELSE phase,
            !.expectedPhase = IF enabled THEN expected + Chunk ELSE expected,
            !.contract = @ /\ keys = observed, !.seen = Append(@, keys)]
Done == s.command = 5 /\ UNCHANGED vars
Next == Transfer \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
MeasuredContract == s.contract
Bounds == s.command \in 1..5 /\ Len(s.seen) = s.command - 1
Terminates == <> (s.command = 5)
=============================================================================
