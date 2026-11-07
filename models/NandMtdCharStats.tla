-------------------------- MODULE NandMtdCharStats --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Core admission can return before touching ops->stats. mtdchar accumulates
\* it before checking the error. Error codes do not initialize output objects.
EXTENDS Integers
CONSTANTS Initialize, Reject
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "core", initialized |-> Initialize, readDefined |-> TRUE]
Core == /\ s.pc = "core"
        /\ s' = [s EXCEPT !.pc = "accumulate",
                          !.initialized = @ \/ ~Reject]
Accumulate == /\ s.pc = "accumulate"
              /\ s' = [s EXCEPT !.pc = "done", !.readDefined = s.initialized]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Core \/ Accumulate \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
DefinedStatistics == s.readDefined
Terminates == <> (s.pc = "done")
=============================================================================
