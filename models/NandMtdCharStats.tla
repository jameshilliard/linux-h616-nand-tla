-------------------------- MODULE NandMtdCharStats --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Core admission can return before touching ops->stats. mtdchar accumulates
\* it before checking the error. Error codes do not initialize output objects.
EXTENDS Integers
CONSTANTS Initialize, Reject, Rounds
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "core", round |-> 1,
             initialized |-> FALSE, readDefined |-> TRUE]
\* stats is declared inside the ioctl loop. Initialization at declaration
\* therefore applies to every iteration, not just the first core call.
Core == /\ s.pc = "core"
        /\ s' = [s EXCEPT !.pc = "accumulate",
                          !.initialized = Initialize \/ ~Reject \/ s.round < Rounds]
Accumulate == /\ s.pc = "accumulate"
              /\ s' = [s EXCEPT !.pc = IF s.round = Rounds THEN "done" ELSE "core",
                                !.readDefined = @ /\ s.initialized,
                                !.round = @+1]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Core \/ Accumulate \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
DefinedStatistics == s.readDefined
Terminates == <> (s.pc = "done")
=============================================================================
