------------------------- MODULE NandBbmInitialization -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* A single marker bit represents XOR with a nonzero randomizer mask.
\* Discovery and ECC decoding are boundaries, not hardware emulation.
EXTENDS Integers
CONSTANTS EarlyScramble, LateScramble, Vendor, Marker, Mutation
VARIABLE s
vars == <<s>>
Xor(a, b) == (a + b) % 2
Mask(enabled) == IF enabled THEN 1 ELSE 0

Init == s = [pc |-> "identify", scramble |-> FALSE, callback |-> FALSE,
             readMarker |-> -1, flashMarker |-> -1]
Identify ==
    /\ s.pc = "identify"
    /\ s' = [s EXCEPT !.scramble = EarlyScramble, !.pc = "attach"]
Attach ==
    /\ s.pc = "attach"
    /\ s' = [s EXCEPT !.scramble = @ \/ Vendor,
        !.callback = IF Mutation = "cached-attach" THEN ~Vendor /\ s.scramble
                     ELSE IF Mutation = "transform-vendor" THEN TRUE ELSE ~Vendor,
        !.pc = "manufacturer"]
Manufacturer ==
    /\ s.pc = "manufacturer"
    /\ s' = [s EXCEPT !.scramble = @ \/ LateScramble, !.pc = "read"]
Compensate == s.callback /\
    (s.scramble \/ Mutation \in {"unconditional-transform", "cached-attach"})
Read ==
    /\ s.pc = "read"
    \* Read a pre-existing image in the selected format, not our own write.
    /\ LET stored == Xor(Marker, Mask(Vendor /\ s.scramble))
           decoded == Xor(stored, Mask(s.scramble))
       IN s' = [s EXCEPT !.readMarker = Xor(decoded, Mask(Compensate)),
                         !.pc = "write"]
Write ==
    /\ s.pc = "write"
    /\ s' = [s EXCEPT !.flashMarker = Xor(Xor(Marker, Mask(Compensate)),
                                         Mask(s.scramble)), !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Identify \/ Attach \/ Manufacturer \/ Read \/ Write \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK == s.scramble \in BOOLEAN /\ s.callback \in BOOLEAN /\
          s.readMarker \in {-1, 0, 1} /\ s.flashMarker \in {-1, 0, 1}
ReadCompatibility == s.pc \in {"write", "done"} => s.readMarker = Marker
WriteCompatibility == s.pc = "done" =>
    s.flashMarker = Xor(Marker, Mask(Vendor /\ s.scramble))
Terminates == <> (s.pc = "done")
=============================================================================
