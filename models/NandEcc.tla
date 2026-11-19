------------------------------ MODULE NandEcc ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, Sequences, FiniteSets
CONSTANTS N, Batch, Dma, Vendor, Scrambling, OobRequired, RawResult, SpareErased, Kinds, Mutation
Steps == 0..(N - 1)
Lanes == 0..3
IsError(k) == k \in {"error", "zero-error", "ones-error"}
IsPattern(k) == k \in {"zero", "ones", "zero-error", "ones-error"}
IsZero(k) == k \in {"zero", "zero-error"}
IsOnes(k) == k \in {"ones", "ones-error"}
RECURSIVE Sum(_)
Sum(sq) == IF Len(sq) = 0 THEN 0 ELSE Head(sq) + Sum(Tail(sq))
Max(a, b) == IF a > b THEN a ELSE b
Min(a, b) == IF a < b THEN a ELSE b

VARIABLE s
vars == <<s>>
Init ==
    \E input \in [Steps -> Kinds] :
    \E raw \in [Steps -> IF RawResult = "mixed" THEN {"bad", "erased", "io"}
                        ELSE {RawResult}] :
    \E delivered \in [Steps -> IF Dma THEN {"dma-a", "dma-b"} ELSE {"old"}] :
      s = [input |-> input, delivered |-> delivered, pc |-> "batch", first |-> 0, count |-> 0, slot |-> 0,
           rawInput |-> raw,
           pid |-> {}, pidReads |-> 0, cacheKey |-> -1, cache |-> [j \in Lanes |-> 0],
           data |-> delivered, oob |-> [j \in Steps |-> "old"],
           consumed |-> {}, errors |-> {}, zeros |-> {}, pendingRaw |-> {},
           corrected |-> 0, failed |-> 0, maxflips |-> 0, erased |-> FALSE,
           captured |-> {}, clobbered |-> FALSE, transportError |-> FALSE,
           tail |-> "not-read"]
BatchUsesVendor == IF Mutation = "dispatch-on-scrambling" THEN Scrambling
                   ELSE Vendor /\ Mutation # "ordinary-batch-for-vendor"
FinishUsesVendor == Vendor /\ Mutation # "ordinary-finish-for-vendor"
BeginBatch ==
    /\ s.pc = "batch" /\ s.first < N
    /\ LET count == Min(Batch, N - s.first)
           slots == 0..(count - 1)
           found == {j \in slots : IsPattern(s.input[s.first + j])}
           errs == {j \in slots : IsError(s.input[s.first + j])}
           bits == {j \in slots : IsOnes(s.input[s.first + j])}
           need == IF BatchUsesVendor /\ Mutation # "skip-error-only-patterns"
                   THEN found # {} ELSE found \ errs # {}
           reads == IF Mutation = "always-read-pattern-id" THEN 1
                    ELSE IF ~need THEN 0
                    ELSE IF Mutation = "read-zero-twice" /\ bits = {} THEN 2
                    ELSE 1
       IN s' = [s EXCEPT !.count = count, !.slot = 0,
                !.pc = IF BatchUsesVendor THEN "vendor" ELSE "ordinary",
                !.captured = {}, !.clobbered = FALSE, !.pendingRaw = {},
                !.cacheKey = IF Mutation = "stale-count-cache" THEN @ ELSE -1,
                !.pidReads = reads, !.pid = IF reads = 0 THEN {} ELSE bits]
LogicalStep == s.first + s.slot
Kind == s.input[LogicalStep]
CountWord == s.slot \div 4
Counts == IF CountWord = s.cacheKey \/
             (Mutation = "reuse-wrong-count-word" /\ s.cacheKey # -1) THEN s.cache
          ELSE [j \in Lanes |-> s.first + 4 * CountWord + j + 1]
SlotCount == Counts[s.slot % 4]

\* Error wins over a simultaneously detected pattern. pendingRaw is a
\* history variable: the C second pass examines saved hardware error bits.
\* Model DMA's main-buffer contents independently of pattern status.
OrdinarySlot ==
    /\ s.pc = "ordinary" /\ s.slot < s.count
    /\ LET i == LogicalStep
           patternFirst == Mutation = "pattern-before-error" /\ IsPattern(Kind)
           raw == IsError(Kind) /\ ~patternFirst
           patterned == ~raw /\ IsPattern(Kind)
           corrected == ~raw /\ ~patterned
           fillMain == patterned /\ (~Dma \/ Scrambling \/ Mutation = "force-dma-pattern-fill")
           ones == IF Mutation = "whole-pattern-word"
                   THEN s.pid # {} ELSE s.slot \in s.pid
           tag == IF raw THEN "old"
                  ELSE IF patterned /\ Mutation # "overwrite-pattern"
                  THEN IF ones THEN "ones" ELSE "zero" ELSE "decoded"
       IN s' = [s EXCEPT
           !.data[i] = IF raw THEN IF ~Dma /\ ~Scrambling THEN "physical" ELSE @
                       ELSE IF fillMain THEN tag ELSE IF Dma THEN @ ELSE "decoded",
           !.oob[i] = IF OobRequired /\ ~raw THEN tag ELSE @,
           !.cache = IF corrected THEN Counts ELSE @,
           !.cacheKey = IF corrected THEN CountWord ELSE @,
           !.corrected = IF corrected THEN @ + SlotCount ELSE @,
           !.maxflips = IF corrected THEN Max(@, SlotCount) ELSE @,
           !.pendingRaw = IF raw THEN @ \cup {i} ELSE @,
           !.consumed = IF raw THEN @ ELSE @ \cup {i},
           !.captured = @ \cup {i},
           !.erased = @ \/ (patterned /\ ones),
           !.slot = @ + 1]

\* Do not let the ordinary step policy touch a firmware page, even on
\* data-only reads. Record both facts (error and pattern may coincide).
VendorSlot ==
    /\ s.pc = "vendor" /\ s.slot < s.count
    /\ LET i == LogicalStep
           exception == IsError(Kind) \/ IsPattern(Kind)
           zero == IsPattern(Kind) /\ s.slot \notin s.pid /\
                   ~(Mutation = "lose-zero-on-error" /\ IsError(Kind))
       IN s' = [s EXCEPT
           !.data[i] = IF Dma THEN @ ELSE "decoded", !.oob[i] = "decoded",
           !.cache = IF exception THEN @ ELSE Counts,
           !.cacheKey = IF exception THEN @ ELSE CountWord,
           !.corrected = IF exception THEN @ ELSE @ + SlotCount,
           !.maxflips = IF exception THEN @ ELSE Max(@, SlotCount),
           !.errors = IF exception THEN @ \cup {i} ELSE @,
           !.zeros = IF zero THEN @ \cup {i} ELSE @,
           !.consumed = @ \cup {i}, !.captured = @ \cup {i},
           !.slot = @ + 1]
StartRaw ==
    /\ s.pc = "ordinary" /\ s.slot = s.count
    /\ s' = [s EXCEPT !.pc = "raw", !.slot = 0]
Raw ==
    /\ s.pc = "raw" /\ s.slot < s.count
    /\ LET i == s.first + s.slot
           raw == i \in s.pendingRaw
           io == raw /\ s.rawInput[i] = "io"
           erased == raw /\ s.rawInput[i] = "erased"
       IN s' = [s EXCEPT
           !.pc = IF io THEN "done" ELSE @,
           !.transportError = @ \/ io,
           !.data[i] = IF raw /\ ~io THEN IF erased THEN "ones"
                                         ELSE IF Scrambling THEN "physical" ELSE @
                       ELSE @,
           !.oob[i] = IF raw /\ ~io THEN IF erased THEN "ones" ELSE "physical" ELSE @,
           !.clobbered = @ \/ raw,
           !.consumed = IF raw /\ ~io THEN @ \cup {i} ELSE @,
           !.corrected = @ + IF erased THEN 1 ELSE 0,
           !.failed = @ + IF raw /\ s.rawInput[i] = "bad" THEN 1 ELSE 0,
           !.maxflips = IF erased THEN Max(@, 1) ELSE @,
           !.erased = @ \/ erased, !.slot = @ + 1]
Advance ==
    /\ s.pc \in {"raw", "vendor"} /\ s.slot = s.count
    /\ s' = [s EXCEPT !.first = @ + s.count, !.clobbered = FALSE,
                     !.pc = IF s.first + s.count = N THEN "finish" ELSE "batch"]
Finish ==
    /\ s.pc = "finish"
    /\ LET zero == FinishUsesVendor /\ s.zeros = Steps
           erased == FinishUsesVendor /\ s.errors # {} /\ SpareErased /\
                     (~zero \/ Mutation = "spare-before-zero")
       IN s' = [s EXCEPT
           !.data = IF erased THEN [i \in Steps |-> "ones"]
                    ELSE IF zero THEN [i \in Steps |-> "zero"] ELSE @,
           !.oob = IF erased THEN [i \in Steps |-> "ones"]
                   ELSE IF zero THEN [i \in Steps |-> "zero"] ELSE @,
           !.failed = IF FinishUsesVendor THEN
                      IF erased THEN 0 ELSE IF zero THEN N ELSE Cardinality(s.errors)
                      ELSE @,
           !.corrected = IF zero \/ erased THEN 0 ELSE @,
           !.maxflips = IF zero \/ erased THEN 0 ELSE @,
           !.tail = IF ~OobRequired \/ (FinishUsesVendor /\ (erased \/ zero)) THEN "not-read"
                    ELSE IF FinishUsesVendor THEN "firmware"
                    ELSE IF s.erased THEN "raw" ELSE "normal",
           !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == BeginBatch \/ OrdinarySlot \/ VendorSlot \/ StartRaw \/ Raw \/ Advance \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)

\* Independent page-level oracle. It uses logical steps, not snapshot/cache
\* state or the implementation's accumulating bitmaps.
AllZero == \A i \in Steps : IsZero(s.input[i])
AnyException == \E i \in Steps : IsError(s.input[i]) \/ IsPattern(s.input[i])
VendorErased == ~AllZero /\ AnyException /\ SpareErased
Correction(i) ==
    IF Vendor THEN
       IF AllZero \/ VendorErased \/ IsError(s.input[i]) \/ IsPattern(s.input[i])
       THEN 0 ELSE i + 1
    ELSE IF IsError(s.input[i]) THEN IF s.rawInput[i] = "erased" THEN 1 ELSE 0
         ELSE IF IsPattern(s.input[i]) THEN 0 ELSE i + 1
ExpectedTag(i) ==
    IF Vendor THEN IF AllZero THEN "zero"
                   ELSE IF VendorErased THEN "ones" ELSE "decoded"
    ELSE IF IsError(s.input[i]) THEN IF s.rawInput[i] = "erased" THEN "ones" ELSE "physical"
         ELSE IF IsPattern(s.input[i]) THEN IF IsOnes(s.input[i]) THEN "ones" ELSE "zero"
              ELSE "decoded"
\* A plain DMA buffer is not rewritten for successful reads or a failed
\* physical erased check. Scrambled patterns are explicitly synthesized.
ExpectedMain(i) ==
    IF ~Dma THEN ExpectedTag(i)
    ELSE IF Vendor THEN IF AllZero \/ VendorErased THEN ExpectedTag(i) ELSE s.delivered[i]
    ELSE IF IsError(s.input[i]) THEN
       IF s.rawInput[i] = "erased" \/ Scrambling THEN ExpectedTag(i) ELSE s.delivered[i]
    ELSE IF IsPattern(s.input[i]) /\ Scrambling THEN ExpectedTag(i) ELSE s.delivered[i]
ExpectedFailures ==
    IF Vendor THEN IF AllZero THEN N ELSE IF VendorErased THEN 0
                   ELSE Cardinality({i \in Steps : IsError(s.input[i]) \/ IsPattern(s.input[i])})
    ELSE Cardinality({i \in Steps : IsError(s.input[i]) /\ s.rawInput[i] = "bad"})
CaptureBeforeRaw == s.clobbered => s.captured = s.first..(s.first + s.count - 1)
\* Count register accesses, not whether the returned bitmap was nonzero.
PatternReadCount == s.pc \in {"ordinary", "vendor", "raw"} =>
    s.pidReads = IF \E i \in s.first..(s.first + s.count - 1) :
                         IsPattern(s.input[i]) /\ (Vendor \/ ~IsError(s.input[i]))
                 THEN 1 ELSE 0
\* Check the boundary before a later recovery or page fill can hide a
\* misclassified slot. Hardware facts here come directly from the input.
PendingErrors == s.pc = "raw" =>
    s.pendingRaw = {i \in s.first..(s.first+s.count-1) : IsError(s.input[i])}
VendorEvidence == Vendor /\ s.pc # "done" =>
    /\ s.pendingRaw = {}
    /\ s.zeros = {i \in s.consumed : IsZero(s.input[i])}
    /\ s.errors = {i \in s.consumed : IsError(s.input[i]) \/ IsPattern(s.input[i])}
    /\ \A i \in s.consumed \cup s.captured :
        s.data[i] = (IF Dma THEN s.delivered[i] ELSE "decoded") /\ s.oob[i] = "decoded"
TransportCorrect == s.pc = "done" =>
    s.transportError = (~Vendor /\ \E i \in Steps : IsError(s.input[i]) /\ s.rawInput[i] = "io")
ExpectedErasedStep == \E i \in Steps : IF IsError(s.input[i]) THEN s.rawInput[i] = "erased"
                                      ELSE IsOnes(s.input[i])
TailCorrect == s.pc = "done" /\ ~s.transportError =>
    s.tail = IF ~OobRequired \/ (Vendor /\ (AllZero \/ VendorErased)) THEN "not-read"
             ELSE IF Vendor THEN "firmware"
             ELSE IF ExpectedErasedStep THEN "raw" ELSE "normal"
ResultCorrect == s.pc = "done" /\ ~s.transportError =>
    /\ s.consumed = Steps
    /\ \A i \in Steps : s.data[i] = ExpectedMain(i)
    /\ \A i \in Steps :
         (Vendor \/ OobRequired \/ IsError(s.input[i])) => s.oob[i] = ExpectedTag(i)
StatsCorrect == s.pc = "done" /\ ~s.transportError =>
    /\ s.corrected = Sum([i \in 1..N |-> Correction(i - 1)])
    /\ s.failed = ExpectedFailures
    /\ s.maxflips = CHOOSE k \in {0} \cup {Correction(i) : i \in Steps} :
                        \A j \in Steps : k >= Correction(j)
Terminates == <> (s.pc = "done")
=============================================================================
