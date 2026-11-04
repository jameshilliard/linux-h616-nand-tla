--------------------------- MODULE NandDiscovery ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Decode/attach/scan-tail composition; CRC arithmetic is checked in exact C.
EXTENDS Integers
CONSTANTS Page, Oob, Step, Strength, Vendor, Engine, CrcCopies, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "read", copy |-> 1, accepted |-> FALSE,
    bytes |-> 0, steps |-> 0, user |-> 0, callback |-> "none", failed |-> FALSE]
Read == /\ s.pc = "read" /\ s.copy <= 3
    /\ s' = IF s.copy \in CrcCopies THEN [s EXCEPT !.pc = "decode"] ELSE
               [s EXCEPT !.copy = @ + 1]
Reject == /\ s.pc = "read" /\ s.copy > 3
    /\ s' = [s EXCEPT !.pc = "done", !.failed = TRUE]
Decode == /\ s.pc = "decode"
    /\ s' = [s EXCEPT !.steps = Page \div Step,
        !.bytes = (14 * Strength + 7) \div 8,
        !.user = IF Vendor /\ Page \div Step > 4 THEN 16 ELSE (Page \div Step) * 4,
        !.pc = "attach"]
Attach == /\ s.pc = "attach"
    /\ LET fits == s.steps > 0 /\ s.steps * s.bytes + s.user <= Oob
           supported == Engine = "host" /\ fits
       IN s' = [s EXCEPT !.accepted = supported,
          !.callback = IF supported THEN "hardware" ELSE
                       IF ~Vendor /\ Page < Step THEN "software" ELSE "none",
          !.failed = ~supported /\ (Vendor \/ Page >= Step), !.pc = "publish"]
Publish == /\ s.pc = "publish"
    /\ s' = [s EXCEPT !.pc = "done",
        !.callback = IF Mutation = "zero-step-hardware" /\ s.steps = 0
                      THEN "hardware" ELSE @]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Read \/ Reject \/ Decode \/ Attach \/ Publish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
HardwareHasGeometry == s.callback = "hardware" => s.steps > 0 /\ s.steps * s.bytes + s.user <= Oob
VendorRequiresHardware == s.pc = "done" /\ Vendor /\ ~s.failed => s.callback = "hardware"
NoPublicationOnBadCrc == CrcCopies = {} => ~s.accepted
Terminates == <> (s.pc = "done")
=============================================================================
