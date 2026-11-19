---- MODULE NandSubpage ----
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, Sequences, FiniteSets

CONSTANTS Status, StepBytes, FirstBatch, Transport, Firmware, Mutation
VARIABLES lo, hi, s
vars == <<lo, hi, s>>
N == Len(Status)
Steps == 0..(N - 1)
PageBytes == N * StepBytes
ASSUME /\ N > 0 /\ StepBytes > 0 /\ FirstBatch \in 1..N
       /\ Transport \in {"dma", "pio"}
       /\ \A k \in Steps : Status[k + 1] \in -3..9

\* Model one nonempty request. The oracle uses byte interval intersection
\* rather than the implementation's division and rounding.
Wanted == {k \in Steps : k * StepBytes < hi /\ (k + 1) * StepBytes > lo}
Expected == IF Firmware THEN Steps ELSE Wanted
Whole == Firmware /\ Mutation # "firmware-request-only"
First == IF Whole THEN 0 ELSE lo \div StepBytes
End == IF Whole THEN N ELSE
       IF Mutation = "round-end-down" THEN hi \div StepBytes
       ELSE (hi + StepBytes - 1) \div StepBytes
InitialBase == IF Transport = "dma" THEN 0 ELSE First
Width(base) == IF Transport = "pio" \/ base # 0 THEN 1
               ELSE IF End < FirstBatch THEN End ELSE FirstBatch

\* Negative statuses describe an ECC exception: -1 is genuinely bad, -2
\* fails the ordinary raw reread, -3 is erased with one corrected bit.
\* For the firmware witnesses the spare is non-erased and there are no
\* pattern flags. Its existing whole-page oracle covers other spare/pattern
\* outcomes; exceptions here are failures, without ordinary raw rereads.
Count(value) == IF value >= 0 THEN value
                ELSE IF value = -3 /\ ~Firmware THEN 1 ELSE 0
Bad(value) == value < 0 /\ (Firmware \/ value = -1)
Io(value) == ~Firmware /\ value = -2
Max(a, b) == IF a > b THEN a ELSE b
RECURSIVE Sum(_)
Sum(ks) == IF ks = {} THEN 0
           ELSE LET k == CHOOSE i \in ks : TRUE
                IN Count(Status[k + 1]) + Sum(ks \ {k})
OracleFailed == Cardinality({k \in Expected : Bad(Status[k + 1])})
OracleIo == \E k \in Expected : Io(Status[k + 1])
OracleMax == CHOOSE m \in 0..9 :
                /\ \A k \in Expected : Count(Status[k + 1]) <= m
                /\ (m = 0 \/ \E k \in Expected : Count(Status[k + 1]) = m)

Init == /\ lo \in 0..(PageBytes - 1)
        /\ hi \in (lo + 1)..PageBytes
        /\ s = [phase |-> "batch", base |-> InitialBase, slot |-> 0,
                raw |-> {}, seen |-> {}, corrected |-> 0,
                failed |-> 0, peak |-> 0, io |-> FALSE]

Step == s.base + s.slot
Selected == Mutation = "account-prefix" \/ Step >= First
\* The DMA status and counter indices remain relative to the hardware
\* batch, even if the first slots in that batch were not requested.
RegSlot == IF Mutation = "renumber-slots" /\ First > s.base /\ Step >= First
           THEN Step - First ELSE s.slot
Value == Status[s.base + RegSlot + 1]
BeginBatch == /\ s.phase = "batch" /\ s.base < End
              /\ s' = [s EXCEPT !.phase = "corrected", !.slot = 0]
              /\ UNCHANGED <<lo, hi>>
Corrected == /\ s.phase = "corrected" /\ s.slot < Width(s.base)
             /\ s' = [s EXCEPT !.slot = @ + 1,
                  !.seen = IF Selected THEN @ \cup {Step} ELSE @,
                  !.failed = IF Selected /\ Firmware /\ Bad(Value) THEN @ + 1 ELSE @,
                  !.corrected = IF Selected /\ Value >= 0 THEN @ + Value ELSE @,
                  !.peak = IF Selected /\ Value >= 0 THEN Max(@, Value) ELSE @]
             /\ UNCHANGED <<lo, hi>>
StartRaw == /\ s.phase = "corrected" /\ s.slot = Width(s.base)
            /\ s' = [s EXCEPT !.phase = "raw", !.slot = 0]
            /\ UNCHANGED <<lo, hi>>
Reread == /\ s.phase = "raw" /\ s.slot < Width(s.base)
          /\ LET take == ~Firmware /\ Value < 0 /\
                         (Selected \/ Mutation = "skip-first-pass-only")
                 \* Once rereading, the NAND column is for the logical
                 \* step, independent of the ECC register slot.
                 value == Status[Step + 1]
             IN s' = [s EXCEPT !.slot = @ + 1,
                 !.raw = IF take THEN @ \cup {Step} ELSE @,
                 !.failed = IF take /\ Bad(value) THEN @ + 1 ELSE @,
                 !.corrected = IF take THEN @ + Count(value) ELSE @,
                 !.peak = IF take THEN Max(@, Count(value)) ELSE @,
                 !.io = take /\ Io(value),
                 !.phase = IF take /\ Io(value) THEN "done" ELSE @]
          /\ UNCHANGED <<lo, hi>>
Advance == /\ s.phase = "raw" /\ s.slot = Width(s.base)
           /\ s' = [s EXCEPT !.phase = "batch", !.base = @ + Width(s.base)]
           /\ UNCHANGED <<lo, hi>>
Finish == /\ s.phase = "batch" /\ s.base >= End
          /\ s' = [s EXCEPT !.phase = "done"] /\ UNCHANGED <<lo, hi>>
Done == s.phase = "done" /\ UNCHANGED vars
Next == BeginBatch \/ Corrected \/ StartRaw \/ Reread \/ Advance \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Terminates == <>(s.phase = "done")

StepScope == s.seen \subseteq Expected /\ s.raw \subseteq Expected
Complete == s.phase # "done" \/ s.io \/
            (s.seen = Expected /\
             s.raw = IF Firmware THEN {} ELSE {k \in Wanted : Status[k + 1] < 0})
Stats == s.phase # "done" \/ s.io \/
         (s.corrected = Sum(Expected) /\ s.failed = OracleFailed /\ s.peak = OracleMax)
\* The ECC hook returns the maximum or a transport errno. The NAND core
\* independently converts an increase in ecc_stats.failed into -EBADMSG.
Hook == IF s.io THEN -5 ELSE s.peak
Caller == IF Hook < 0 THEN Hook ELSE IF s.failed > 0 THEN -74 ELSE Hook
Result == s.phase # "done" \/
          (Hook = (IF OracleIo THEN -5 ELSE OracleMax) /\
           Caller = (IF OracleIo THEN -5 ELSE IF OracleFailed > 0 THEN -74 ELSE OracleMax))
====
