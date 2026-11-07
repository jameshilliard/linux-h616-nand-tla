-------------------------- MODULE NandMtdCharTrace --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Check actual extracted ioctl calls against both the modeled planner and an
\* independent stream-address oracle. Fatal-return progress is not committed.
EXTENDS Integers, Sequences
CONSTANTS Trials, Mutation, CheckPlan
VARIABLES r, s
vars == <<r,s>>
Planner == INSTANCE NandMtdCharChunks
           WITH Requests <- <<>>, Faults <- FALSE, Mutation <- "none"
Min(a,b) == IF a < b THEN a ELSE b
Max(a,b) == IF a > b THEN a ELSE b
Cover(n,w) == IF n = 0 THEN 0 ELSE (n-1) \div w + 1
Init == /\ r \in {Trials[i] : i \in 1..Len(Trials)}
        /\ s = [pc |-> "step", i |-> 1, at |-> r.start,
                dd |-> 0, od |-> 0, mapping |-> TRUE, plan |-> TRUE,
                counts |-> TRUE, unique |-> TRUE, lastPage |-> -1,
                code |-> 0]
Step ==
    /\ s.pc = "step" /\ s.i <= Len(r.calls)
    /\ LET c == r.calls[s.i]
           a == c[1]
           d == c[2]
           o == c[3]
           rd == c[4]
           ro == c[5]
           p == Planner!Plan(s.at,r.data-s.dd,r.oob-s.od)
           rows == Max(IF rd = 0 THEN 0 ELSE Cover((a % r.width)+rd,r.width),
                       IF ro = 0 THEN 0 ELSE Cover(ro,r.avail))
           first == a \div r.width
           ok == c[6] \in {0,-117,-74}
       IN s' = [s EXCEPT
           !.plan = @ /\ a = s.at /\ d = p.data /\ o = p.oob,
           !.mapping = @ /\ (rd = 0 \/ a = r.start+s.dd) /\
                   (ro = 0 \/ first*r.avail =
                    (r.start \div r.width)*r.avail+s.od),
           !.counts = @ /\ rd \in 0..d /\ ro \in 0..o /\
                     d <= Min(r.data,r.erase) /\
                     o <= Min(r.oob,Max(r.erase,r.spare)),
           !.unique = @ /\ (rows = 0 \/ first > s.lastPage),
           !.lastPage = IF rows > 0 THEN first+rows-1 ELSE @,
           !.dd = @ + IF ok THEN rd ELSE 0,
           !.od = @ + IF ok THEN ro ELSE 0,
           !.at = @ + IF ~ok THEN 0 ELSE IF r.hasData
                      THEN rd ELSE (ro \div r.avail)*r.width,
           !.code = c[6], !.i = @+1]
Finish == /\ s.pc = "step" /\ s.i > Len(r.calls)
          /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == /\ UNCHANGED r /\ (Step \/ Finish \/ Done)
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
SourcePlan == ~CheckPlan \/ s.plan
SourceBytes == s.mapping
SourceBounds == s.counts
SourceOnce == s.unique
SourceReturn == s.pc = "done" =>
                (r.code = s.code /\
                 (~r.read \/ r.reported = <<s.dd,s.od>>))
Terminates == <> (s.pc = "done")
=============================================================================
