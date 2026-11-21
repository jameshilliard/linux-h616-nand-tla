---- MODULE NandSubpageProgram ----
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, FiniteSets
CONSTANTS Steps, First, End, OobSteps, OobRequired, OobMode, Mutation
ASSUME /\ Steps \in Nat /\ Steps > 0 /\ First \in 0..(Steps - 1)
       /\ End \in (First + 1)..Steps /\ OobSteps \subseteq 0..(Steps - 1)
       /\ OobMode \in {"none", "auto", "place"}
Requested == First..(End - 1)
AllowedOob == OobSteps \cap Requested
VARIABLES phase, step, data, oob
vars == <<phase, step, data, oob>>
Init == /\ phase = "begin" /\ step = First /\ data = {} /\ oob = {}
Begin == /\ phase = "begin" /\ phase' = "program"
         /\ UNCHANGED <<step, data, oob>>
Program == /\ phase = "program" /\ step < End /\ step' = step + 1
           /\ data' = IF Mutation = "write-page-tail" THEN 0..(Steps - 1)
                     ELSE data \cup {step}
           /\ oob' = oob
           /\ phase' = IF step + 1 = End THEN
                         IF OobRequired THEN "oob" ELSE "finish"
                       ELSE "program"
Oob == /\ phase = "oob"
       /\ oob' = IF Mutation = "oob-outside" THEN OobSteps ELSE AllowedOob
       /\ UNCHANGED <<step, data>> /\ phase' = "finish"
Finish == /\ phase = "finish" /\ phase' = "done"
          /\ UNCHANGED <<step, data, oob>>
DoneAction == /\ phase = "done" /\ UNCHANGED vars
Next == Begin \/ Program \/ Oob \/ Finish \/ DoneAction
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Terminates == <>(phase = "done")
DataScope == data \subseteq Requested
OobScope == oob \subseteq AllowedOob
DataComplete == phase # "done" \/ data = Requested
OobComplete == phase # "done" \/ oob = (IF OobRequired THEN AllowedOob ELSE {})
====
