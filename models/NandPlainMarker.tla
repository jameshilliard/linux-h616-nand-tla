---- MODULE NandPlainMarker ----
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Naturals
CONSTANT Mutation
VARIABLES kind, raw, corrected, noise, want, laterPattern, tailFails, rawFails, s
inputs == <<kind, raw, corrected, noise, want, laterPattern, tailFails, rawFails>>
vars == <<inputs, s>>

\* The pair of reserved marker bytes is represented by its number of zero
\* bits. Zero denotes 0xffff, 16 denotes 0x0000. Status is an independent
\* input: this does not predict how silicon classifies a factory-marked page.
Init == /\ kind \in {"ordinary", "error", "ones", "zeroes"}
        /\ raw \in {0, 1, 8, 16} /\ corrected \in {0, 8}
        /\ noise \in {0, 1, 16, 17} /\ want \in BOOLEAN /\ laterPattern \in BOOLEAN
        /\ tailFails \in BOOLEAN /\ rawFails \in BOOLEAN
        /\ s = [phase |-> "first", marker |-> 99, need |-> FALSE, reads |-> 0,
                correctedBits |-> 0, failed |-> FALSE, io |-> FALSE]
Pattern == kind \in {"ones", "zeroes"}
First ==
  /\ s.phase = "first"
  /\ LET bits == noise + (IF Mutation = "count-marker" THEN raw ELSE 0)
         erased == kind = "error" /\ bits <= 16
         marker == CASE kind = "ordinary" -> IF Mutation = "bypass-correction" THEN raw ELSE corrected
                        [] kind = "error" -> IF erased /\ Mutation = "erase-marker" THEN 0 ELSE raw
                        [] kind = "ones" -> 0
                        [] OTHER -> 16
     IN s' = [s EXCEPT !.phase = "tail", !.marker = IF want THEN marker ELSE 99,
              !.need = (want \/ Mutation = "read-data-only") /\
                       (Pattern \/ (laterPattern /\ Mutation = "wrong-step")) /\
                       Mutation # "trust-pattern",
              !.correctedBits = IF erased THEN bits ELSE 0,
              !.failed = kind = "error" /\ ~erased]
  /\ UNCHANGED inputs
Tail ==
  /\ s.phase = "tail"
  /\ LET failed == want /\ tailFails
     IN s' = [s EXCEPT !.phase = IF failed THEN "done" ELSE "marker",
              !.io = failed,
              !.reads = IF failed /\ s.need /\ Mutation = "read-before-tail" THEN 1 ELSE 0]
  /\ UNCHANGED inputs
Marker ==
  /\ s.phase = "marker"
  /\ s' = [s EXCEPT !.phase = "done", !.reads = IF s.need THEN 1 ELSE 0,
           !.marker = IF s.need /\ ~rawFails THEN raw ELSE @,
           !.io = s.need /\ rawFails /\ Mutation # "ignore-marker-io"]
  /\ UNCHANGED inputs
Done == s.phase = "done" /\ UNCHANGED vars
Next == First \/ Tail \/ Marker \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Terminates == <>(s.phase = "done")
Source == s.phase # "done" \/ s.io \/ ~want \/
          s.marker = (IF kind = "ordinary" THEN corrected ELSE raw)
Stats == s.phase = "first" \/
         (s.correctedBits = (IF kind = "error" /\ noise <= 16 THEN noise ELSE 0)
          /\ s.failed = (kind = "error" /\ noise > 16))
Traffic == s.phase # "done" \/
           s.reads = (IF want /\ Pattern /\ ~tailFails THEN 1 ELSE 0)
Io == s.phase # "done" \/
      s.io = (want /\ (tailFails \/ (Pattern /\ rawFails)))
====
