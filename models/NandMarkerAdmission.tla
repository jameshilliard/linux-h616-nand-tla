---- MODULE NandMarkerAdmission ----
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Naturals
CONSTANT Mutation
VARIABLES marker, width, firmware, onHost, geometryOK, state
inputs == <<marker, width, firmware, onHost, geometryOK>>
vars == <<inputs, state>>

\* The detected marker is an input from NAND identification, not an
\* address the controller driver can silently change. Typical values
\* are 0 and 5; vary the rest to catch tests of field width instead.
Init == /\ marker \in 0..6 /\ width \in {4, 8, 16}
        /\ firmware \in BOOLEAN /\ onHost \in BOOLEAN /\ geometryOK \in BOOLEAN
        /\ state = [phase |-> "attach", accepted |-> FALSE, offset |-> marker]
Attach == /\ state.phase = "attach"
          /\ LET vendorOK == CASE Mutation \in {"no-guard", "relocate"} -> TRUE
                                   [] Mutation = "within-field" -> marker < width
                                   [] OTHER -> marker = 0
                 ordinaryOK == CASE Mutation = "ordinary-no-guard" -> TRUE
                                    [] Mutation = "ordinary-within-field" -> marker < width
                                    [] OTHER -> marker < 2
                 ok == IF firmware THEN onHost /\ vendorOK /\ ordinaryOK
                       ELSE IF onHost THEN ordinaryOK
                       ELSE Mutation # "restrict-software" \/ ordinaryOK
             IN state' = [phase |-> "done", accepted |-> geometryOK /\ ok,
                          offset |-> IF firmware /\ Mutation = "relocate" THEN 0 ELSE marker]
          /\ UNCHANGED inputs
Done == state.phase = "done" /\ UNCHANGED vars
Next == Attach \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Terminates == <>(state.phase = "done")
\* Mark-bad writes use the core offset, but firmware examines byte zero.
MarkerRoundTrips == state.phase # "done" \/ ~state.accepted \/ ~firmware \/
                    (state.offset = marker /\ marker < width /\ marker = 0)
OrdinaryMarkerProtected == state.phase # "done" \/ ~state.accepted \/ firmware \/ ~onHost \/
                           (state.offset = marker /\ marker \in {0, 1})
SoftwareUnchanged == state.phase # "done" \/ firmware \/ onHost \/ state.accepted = geometryOK
GeometryPreserved == state.phase # "done" \/
                     ((~state.accepted \/ geometryOK) /\
                      (~(geometryOK /\ onHost /\ (IF firmware THEN marker = 0 ELSE marker \in {0, 1}))
                       \/ state.accepted) /\ (~(firmware /\ ~onHost) \/ ~state.accepted))
====
