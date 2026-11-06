--------------------------- MODULE NandTimingFold ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* A quotient of all timing vectors: only the maximum prefix requirement is
\* retained. Durations are selected afresh at each of the 19 source positions.
EXTENDS Integers, Sequences
CONSTANTS Divisors, Durations, AccessTimes, Mutation
VARIABLE s
vars == <<s>>
Max(a,b) == IF a > b THEN a ELSE b
Need(n,d) == n \div d + IF n % d = 0 THEN 0 ELSE 1
Round(n,d) == IF Mutation = "floor" THEN n \div d ELSE (n+d-1) \div d
Init == s = [pc |-> "fold", pos |-> 1, period |-> 0, oracle |-> 0]
Merge ==
    /\ s.pc = "fold"
    /\ s.pos <= Len(Divisors)
    /\ \E n \in Durations :
        LET d == Divisors[s.pos]
            p == IF Mutation = "overwrite" \/ n > s.period*d
                 THEN Round(n,d) ELSE s.period
        IN s' = [s EXCEPT !.pos = @+1, !.period = p,
                          !.oracle = Max(@,Need(n,d))]
ReadAccess ==
    /\ s.pc = "fold" /\ s.pos = Len(Divisors)+1
    /\ \E n \in AccessTimes, hold \in {FALSE, TRUE} :
        s' = [s EXCEPT !.pc = "done",
              !.period = IF ~hold /\ Mutation # "skip-rea"
                         THEN Max(@,n) ELSE @,
              !.oracle = IF ~hold THEN Max(@,n) ELSE @]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Merge \/ ReadAccess \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ExactMinimum == s.period = s.oracle
Nonnegative == s.period >= 0 /\ s.pos \in 1..(Len(Divisors)+1)
Terminates == <> (s.pc = "done")
=============================================================================
