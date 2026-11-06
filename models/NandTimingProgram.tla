-------------------------- MODULE NandTimingProgram --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Full setup_interface calculation, checked against extracted C observations.
\* Clock responses are inputs, not a simulation of the common-clock framework.
EXTENDS Integers, Sequences
CONSTANTS Divisors, Tables, Trials, Mutation
VARIABLE s
vars == <<s>>
Ceil(n,d) == n \div d + IF n % d = 0 THEN 0 ELSE 1
Max(a,b) == IF a > b THEN a ELSE b
q == s.input
ClockNs == IF Mutation = "floor-ns" THEN s.period \div 1000 ELSE Ceil(s.period,1000)
Init == \E r \in 1..Len(Trials) :
    s = [input |-> Trials[r], pc |-> "entry", pos |-> 1, period |-> 0,
         slot |-> 1, code |-> 0, enc |-> <<>>, cfg |-> 291,
         rate |-> 12345, ctl |-> 256, calls |-> 0, ret |-> 0]
Enter ==
    /\ s.pc = "entry"
    /\ s' = IF q.invalid THEN [s EXCEPT !.pc = "done", !.ret = -524]
            ELSE [s EXCEPT !.pc = "fold"]
Merge ==
    /\ s.pc = "fold" /\ s.pos <= 19
    /\ s' = [s EXCEPT !.period = Max(@,Ceil(q.d[s.pos],Divisors[s.pos])),
                       !.pos = @+1]
ReadAccess ==
    /\ s.pc = "fold" /\ s.pos = 20
    /\ s' = [s EXCEPT !.period = IF q.d[21] = 0 THEN Max(@,q.d[20]) ELSE @,
                       !.pc = "lookup"]
Lookup ==
    /\ s.pc = "lookup"
    /\ LET n == q.d[s.slot+15]
           need == IF Mutation = "floor-lookup" THEN n \div s.period
                   ELSE Ceil(n,s.period)
       IN s' = IF Tables[s.slot][s.code+1] >= need
               THEN [s EXCEPT !.enc = Append(@,s.code), !.code = 0,
                     !.slot = @+1, !.pc = IF s.slot = 4 THEN "check" ELSE @]
               ELSE IF s.code = 3
               THEN [s EXCEPT !.ret = -22, !.pc = "done"]
               ELSE [s EXCEPT !.code = @+1]
Check ==
    /\ s.pc = "check"
    /\ s' = [s EXCEPT !.pc = IF q.check /\ Mutation # "write-check-only"
                            THEN "done" ELSE "cache"]
Cache ==
    /\ s.pc = "cache"
    /\ s' = [s EXCEPT !.cfg = s.enc[1] + s.enc[2]*4 + s.enc[3]*16 +
                    s.enc[4]*64 + (IF Mutation = "cad-overlap" THEN 7*64 ELSE 7*256),
                      !.rate = 1000000000 \div ClockNs,
                      !.pc = "clock"]
Clock ==
    /\ s.pc = "clock"
    /\ s' = [s EXCEPT !.calls = @+1,
                      !.ret = IF q.rounded <= 0 THEN -22 ELSE 0,
                      !.pc = IF q.rounded <= 0 THEN "done" ELSE "edo"]
Edo ==
    /\ s.pc = "edo"
    /\ LET ns == 1000000000 \div
                 (IF Mutation = "edo-requested" THEN s.rate ELSE q.rounded)
       IN s' = [s EXCEPT !.ctl = IF ns*2 < 30 \/ ns*1000 < q.d[20]
                                THEN 256 ELSE 0, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Enter \/ Merge \/ ReadAccess \/ Lookup \/ Check \/ Cache \/ Clock \/ Edo \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
LookupFits == \A i \in 1..Len(s.enc) :
    Tables[i][s.enc[i]+1]*s.period >= q.d[i+15]
LookupMinimal == \A i \in 1..Len(s.enc) :
    \A j \in 1..s.enc[i] : Tables[i][j]*s.period < q.d[i+15]
EncodingFits == \A i \in 1..Len(s.enc) : s.enc[i] \in 0..3
ValidDivisors == (s.pc = "lookup" => s.period > 0) /\
                 (s.pc = "cache" => ClockNs > 0)
CheckOnlyPure == q.check => s.cfg = 291 /\ s.rate = 12345 /\
                            s.ctl = 256 /\ s.calls = 0
ClockSafe == s.pc = "done" /\ s.ret = 0 /\ ~q.check /\ ~q.invalid =>
    (1000000000 \div q.rounded) >= Ceil(s.period,1000)
SourceAgreement == s.pc = "done" =>
    <<s.ret, s.cfg, s.rate, s.ctl, s.calls>> = q.observed
Terminates == <> (s.pc = "done")
=============================================================================
