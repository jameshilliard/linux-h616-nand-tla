---- MODULE NandMarkerAdmission ----
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Naturals
CONSTANT Mutation
VARIABLES marker, width, firmware, geometryOK, state
vars == <<marker, width, firmware, geometryOK, state>>

\* The detected marker is an input from NAND identification, not an
\* address the controller driver can silently change. Typical values
\* are 0 and 5; vary the rest to catch tests of field width instead.
Init == /\ marker \in 0..6 /\ width \in {4, 8, 16}
        /\ firmware \in BOOLEAN /\ geometryOK \in BOOLEAN
        /\ state = [phase |-> "attach", accepted |-> FALSE, offset |-> marker]
Attach == /\ state.phase = "attach"
          /\ LET ok == IF Mutation = "no-guard" THEN TRUE
                       ELSE IF Mutation = "within-field" THEN marker < width
                       ELSE IF Mutation = "relocate" THEN TRUE ELSE marker = 0
                 apply == firmware \/ Mutation = "ordinary-too"
             IN state' = [phase |-> "done", accepted |-> geometryOK /\ (~apply \/ ok),
                          offset |-> IF firmware /\ Mutation = "relocate" THEN 0 ELSE marker]
          /\ UNCHANGED <<marker, width, firmware, geometryOK>>
Done == state.phase = "done" /\ UNCHANGED vars
Next == Attach \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Terminates == <>(state.phase = "done")
\* Mark-bad writes use the core offset, but firmware examines byte zero.
MarkerRoundTrips == state.phase # "done" \/ ~state.accepted \/ ~firmware \/
                    (state.offset = marker /\ marker < width /\ marker = 0)
OrdinaryUnchanged == state.phase # "done" \/ firmware \/ state.accepted = geometryOK
GeometryPreserved == state.phase # "done" \/
                     ((~state.accepted \/ geometryOK) /\
                      (~(geometryOK /\ firmware /\ marker = 0) \/ state.accepted))
====
