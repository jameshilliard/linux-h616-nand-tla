------------------------------ MODULE NandTiming ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Time is integral picoseconds; the real driver rounds a period to ns before
\* asking CCF for a clock. Counts below are the actual A10/H616 lookup entries.
EXTENDS Integers, Sequences
CONSTANTS Table, Duration, Period, RoundedPeriod, ReadAccess, CheckOnly, Mutation
VARIABLE s
vars == <<s>>
Ceil(n,d) == (n + d - 1) \div d
Cycles == IF Mutation = "floor-cycles" THEN Duration \div Period ELSE Ceil(Duration,Period)
Init == s = [pc |-> "lookup", index |-> 1, rejected |-> FALSE,
             programmed |-> FALSE, ns |-> 0, edo |-> FALSE]
Lookup ==
    /\ s.pc = "lookup"
    /\ s' = IF Table[s.index] >= Cycles THEN [s EXCEPT !.pc = "clock"]
            ELSE IF s.index = Len(Table) THEN [s EXCEPT !.rejected = TRUE, !.pc = "done"]
            ELSE [s EXCEPT !.index = @ + 1]
Clock ==
    /\ s.pc = "clock"
    /\ s' = IF CheckOnly THEN [s EXCEPT !.pc = "done"] ELSE
        [s EXCEPT !.ns = RoundedPeriod, !.pc = "program"]
Program ==
    /\ s.pc = "program"
    /\ s' = [s EXCEPT !.programmed = TRUE, !.pc = "done",
        !.edo = 2*s.ns < 30 \/ s.ns*1000 < ReadAccess]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Lookup \/ Clock \/ Program \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
LookupSafe == s.pc # "lookup" /\ ~s.rejected => Table[s.index]*Period >= Duration
MinimalEncoding == s.pc # "lookup" /\ ~s.rejected =>
    \A i \in 1..(s.index-1) : Table[i]*Period < Duration
ActualClockSafe == s.programmed => Table[s.index]*s.ns*1000 >= Duration
CheckOnlyPure == CheckOnly => ~s.programmed
RejectionCorrect == s.rejected => Table[Len(Table)]*Period < Duration
Terminates == <> (s.pc = "done")
=============================================================================
