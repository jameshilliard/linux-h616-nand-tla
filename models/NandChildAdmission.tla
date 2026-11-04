--------------------------- MODULE NandChildAdmission --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Device-tree availability must be checked before claiming CS or scanning.
\* Status is immutable during probe. OF reference ownership is not modeled.
EXTENDS Integers, Sequences, FiniteSets
CONSTANTS Nodes, FilterUnavailable, Mutation
VARIABLE s
vars == <<s>>
Available(n) == n.status \in {"absent", "okay", "ok"}
Init == s = [pc |-> "next", i |-> 1, assigned |-> {}, attempted |-> {},
             scanned |-> {}, published |-> {}, result |-> "pending"]
NextNode ==
    /\ s.pc = "next"
    /\ s' = IF s.i > Len(Nodes) THEN [s EXCEPT !.pc = "done", !.result = "ok"]
        ELSE IF FilterUnavailable /\ ~Available(Nodes[s.i])
             THEN [s EXCEPT !.i = @ + 1]
        ELSE [s EXCEPT !.pc = "claim"]
Claim ==
    /\ s.pc = "claim"
    /\ s' = IF Nodes[s.i].cs \in s.assigned
        THEN [s EXCEPT !.pc = "rollback", !.result = "duplicate"]
        ELSE [s EXCEPT !.assigned = @ \cup {Nodes[s.i].cs}, !.pc = "scan"]
Scan ==
    /\ s.pc = "scan"
    /\ s' = [s EXCEPT !.attempted = @ \cup {s.i},
        !.scanned = IF Nodes[s.i].present THEN @ \cup {s.i} ELSE @,
        !.pc = IF Nodes[s.i].present THEN "register" ELSE "rollback",
        !.result = IF Nodes[s.i].present THEN @ ELSE "absent"]
Register ==
    /\ s.pc = "register"
    /\ s' = IF Nodes[s.i].registerOK
        THEN [s EXCEPT !.published = @ \cup {s.i}, !.i = @ + 1, !.pc = "next"]
        ELSE [s EXCEPT !.pc = "rollback", !.result = "registration"]
Rollback ==
    /\ s.pc = "rollback"
    /\ s' = [s EXCEPT !.scanned = {}, !.published = {}, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Step == NextNode \/ Claim \/ Scan \/ Register \/ Rollback
Next == Step \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Step)
OnlyAvailableScanned == \A i \in s.attempted : Available(Nodes[i])
OnlyAvailableClaimed == s.assigned \subseteq
    {Nodes[i].cs : i \in {j \in 1..Len(Nodes) : Available(Nodes[j])}}
PublishedScanned == s.published \subseteq s.scanned
FailureUnwinds == s.pc = "done" /\ s.result # "ok" => s.published = {} /\ s.scanned = {}
Terminates == <> (s.pc = "done")
=============================================================================
