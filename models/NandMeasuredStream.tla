------------------------- MODULE NandMeasuredStream -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* H616 NORMAL command observations; not PAGE_OP/ECC_OP or older NFCs.
\* XOR keys are distinct symbolic positions. The bit-level replay separately
\* checks captured keystream digests, including accidental equal key bytes.
EXTENDS Integers, Sequences
CONSTANTS Size, WriteChunk, ReadChunk, PageMode, Mutation
VARIABLE s
vars == <<s>>
Min(a,b) == IF a < b THEN a ELSE b
Init == s = [pc |-> "write", pos |-> 0, phase |-> 0,
             media |-> <<>>, result |-> <<>>, observed |-> TRUE]
Write == /\ s.pc = "write" /\ s.pos < Size
    /\ LET n == Min(WriteChunk, Size - s.pos)
           phase == IF Mutation = "writer-continuous" THEN s.pos ELSE 0
           encoded == [i \in 1..n |-> <<s.pos + i, phase + i>>]
       IN s' = [s EXCEPT !.media = @ \o encoded, !.pos = @ + n,
            !.observed = @ /\ phase = 0]
Turnaround == /\ s.pc = "write" /\ s.pos = Size
    /\ s' = [s EXCEPT !.pc = "read", !.pos = 0, !.phase = 0]
Read == /\ s.pc = "read" /\ s.pos < Size
    /\ LET n == Min(ReadChunk, Size - s.pos)
           phase == IF PageMode \/ Mutation = "reader-continuous" THEN s.pos ELSE 0
           observedPhase == IF PageMode THEN s.pos ELSE 0
           tail == n % 4 # 0 /\ Mutation # "omit-read-tail"
           keys == [i \in 1..n |-> IF tail /\ i = n THEN 0 ELSE phase + i]
           expected == [i \in 1..n |-> IF n % 4 # 0 /\ i = n THEN 0
                                      ELSE observedPhase + i]
           decoded == [i \in 1..n |-> IF s.media[s.pos + i][2] = keys[i]
                         THEN s.media[s.pos + i][1] ELSE -1]
       IN s' = [s EXCEPT !.result = @ \o decoded, !.pos = @ + n,
            !.phase = phase + n, !.observed = @ /\ keys = expected]
Finish == /\ s.pc = "read" /\ s.pos = Size
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Write \/ Turnaround \/ Read \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
MeasuredContract == s.observed
RoundTrip == s.pc = "done" => s.result = [i \in 1..Size |-> i]
Bounds == Len(s.media) <= Size /\ Len(s.result) <= Size
Terminates == <> (s.pc = "done")
=============================================================================
