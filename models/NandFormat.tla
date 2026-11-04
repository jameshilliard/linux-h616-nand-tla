----------------------------- MODULE NandFormat -----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Symbolic ECC/randomizer composition; parity is a token, NOT BCH arithmetic.
\* PIO and DMA consume identical segment identities through different batches.
EXTENDS Integers, Sequences
CONSTANTS Pages, Steps, WriteBatch, ReadBatch, Randomized, ReaderCompatible, Mutation
VARIABLE s
vars == <<s>>
Count == Pages * Steps
Page(i) == (i - 1) \div Steps
Step(i) == (i - 1) % Steps
BatchEnd(i,n) == IF Step(i) + n > Steps THEN (Page(i) + 1) * Steps ELSE i + n - 1
Payload(i) == <<Page(i), Step(i), "data", "protected">>
Seed(i) == IF Randomized THEN <<Page(i), "ecc-seed">> ELSE <<"off">>
Encoded(i, key) == [payload |-> Payload(i), seed |-> key, parity |-> <<"ecc", Payload(i)>>]
Init == s = [pc |-> "seed", index |-> 1, seed |-> <<"unset">>,
    media |-> [i \in 1..Count |-> Encoded(i, Seed(i))], written |-> {}, output |-> <<>>,
    batchEnd |-> WriteBatch, enabled |-> FALSE, result |-> "unset"]
Configure ==
    /\ s.pc = "seed"
    /\ s' = [s EXCEPT !.seed = IF Mutation = "stale-seed" /\ s.index > Steps
                               THEN Seed(1) ELSE Seed(s.index),
                      !.batchEnd = BatchEnd(s.index, WriteBatch),
                      !.enabled = Randomized, !.pc = "program"]
Program ==
    /\ s.pc = "program"
    /\ s' = [s EXCEPT !.media = [i \in 1..Count |->
        IF i \in s.index..s.batchEnd THEN Encoded(i, s.seed) ELSE s.media[i]],
        !.written = @ \cup (s.index..s.batchEnd),
        !.index = s.batchEnd + 1, !.pc = "next-write"]
NextWrite ==
    /\ s.pc = "next-write"
    /\ s' = IF s.index > Count THEN
        [s EXCEPT !.pc = "read", !.index = 1, !.enabled = FALSE, !.seed = <<"unset">>]
       ELSE [s EXCEPT !.pc = "seed"]
Read ==
    /\ s.pc = "read"
    /\ LET last == BatchEnd(s.index, ReadBatch)
           decoded(i) == IF ReaderCompatible /\ s.media[i].seed = Seed(i)
                          THEN s.media[i].payload ELSE <<"incompatible">>
       IN s' = [s EXCEPT !.output = @ \o [j \in 1..(last-s.index+1) |-> decoded(s.index+j-1)],
             !.index = last + 1, !.pc = IF last = Count THEN "finish" ELSE "read"]
Finish ==
    /\ s.pc = "finish"
    /\ s' = [s EXCEPT !.pc = "done", !.result = IF ReaderCompatible THEN "ok" ELSE "incompatible"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Configure \/ Program \/ NextWrite \/ Read \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
MediaFormat == \A i \in s.written : s.media[i] = Encoded(i, Seed(i))
RoundTrip == s.result = "ok" => s.output = [i \in 1..Count |-> Payload(i)]
FormatBoundary == ~ReaderCompatible => s.result # "ok"
CleanExit == s.pc = "done" => ~s.enabled
Terminates == <> (s.pc = "done")
=============================================================================
