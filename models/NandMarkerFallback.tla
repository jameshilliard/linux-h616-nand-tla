---- MODULE NandMarkerFallback ----
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Naturals
CONSTANT Mutation
VARIABLES requested, s
vars == <<requested, s>>

\* A10/A23 fixed layout, 512+16 x8 NAND, requested step 1024. Hardware
\* attach sees zero whole steps; the NAND core selects software Hamming
\* only after controller attach succeeds. Keep the layout owner separate
\* from the ECC engine: a change of engine does not remove an installed layout.
Init == /\ requested \in {"host", "hamming", "none"}
        /\ s = [pc |-> "attach", layout |-> "unset", engine |-> requested, parity |-> {}]
Attach ==
    /\ s.pc = "attach"
    /\ LET reject == IF Mutation = "guard-software" THEN TRUE
                     ELSE requested = "host" /\ Mutation # "no-guard"
       IN s' = [s EXCEPT !.pc = IF reject THEN "rejected" ELSE "core",
                         !.layout = IF ~reject /\ requested = "host" THEN "fixed" ELSE @]
    /\ UNCHANGED requested
SelectEngine ==
    /\ s.pc = "core"
    /\ s' = [s EXCEPT !.pc = "layout",
                      !.engine = IF @ = "host" THEN "hamming" ELSE @,
                      !.layout = IF @ = "unset" THEN "small-page" ELSE @]
    /\ UNCHANGED requested
PlaceParity ==
    /\ s.pc = "layout"
    /\ LET steps == 512 \div 256
           bytes == 3
           fixed == UNION { LET start == i * (4 + bytes) + 4
                            IN start..(start + bytes - 1) : i \in 0..(steps - 1)}
           small == 0..3 \cup 6..(6 + steps * bytes - 4 - 1)
       IN s' = [s EXCEPT !.pc = "done",
                         !.parity = IF s.engine = "none" THEN {}
                                    ELSE IF s.layout = "fixed" THEN fixed ELSE small]
    /\ UNCHANGED requested
Finished == s.pc \in {"rejected", "done"}
Done == Finished /\ UNCHANGED vars
Next == Attach \/ SelectEngine \/ PlaceParity \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Terminates == <>Finished
MarkerSafe == s.pc # "done" \/ 5 \notin s.parity
HostRejected == ~Finished \/ requested # "host" \/ s.pc = "rejected"
ExplicitChoice == ~Finished \/ requested = "host" \/
                  (s.pc = "done" /\ s.engine = requested /\ s.layout = "small-page")
====
