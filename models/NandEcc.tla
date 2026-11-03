------------------------------ MODULE NandEcc ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, Sequences, FiniteSets
CONSTANTS N, Batch, Vendor, OobRequired, RawResult, SpareErased, Kinds, Mutation
Steps == 0..(N - 1)
Slots == 0..(Batch - 1)
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
      s = [input |-> input, pc |-> "batch", first |-> 0, count |-> 0, slot |-> 0,
           rawInput |-> raw,
           pid |-> {}, cacheKey |-> -1, cache |-> [j \in Slots |-> 0],
           data |-> [j \in Steps |-> "old"], oob |-> [j \in Steps |-> "old"],
           consumed |-> {}, errors |-> {}, zeros |-> {},
           corrected |-> 0, failed |-> 0, maxflips |-> 0, erased |-> FALSE,
           captured |-> {}, clobbered |-> FALSE, transportError |-> FALSE]
BeginBatch ==
    /\ s.pc = "batch" /\ s.first < N
    /\ LET count == Min(Batch, N - s.first)
       IN s' = [s EXCEPT !.count = count, !.slot = 0, !.pc = "good",
                !.captured = {}, !.clobbered = FALSE,
                !.cacheKey = IF Mutation = "stale-count-cache" THEN @ ELSE -1,
                !.pid = {j \in 0..(count - 1) : IsOnes(s.input[s.first + j])}]
Good ==
    /\ s.pc = "good" /\ s.slot < s.count
    /\ LET i == s.first + s.slot
           kind == s.input[i]
           word == s.slot \div 4
           cache == IF word = s.cacheKey THEN s.cache
                    ELSE [j \in Slots |-> s.first + j + 1]
           count == cache[s.slot]
           patterned == IsPattern(kind) /\ ~IsError(kind)
           ones == IF Mutation = "whole-pattern-word"
                   THEN s.pid # {} ELSE s.slot \in s.pid
           genericData == IF patterned THEN IF ones THEN "ones" ELSE "zero"
                          ELSE IF IsError(kind) THEN "old" ELSE "decoded"
           useCount == IF Vendor THEN ~IsError(kind) /\ ~IsPattern(kind)
                       ELSE ~IsError(kind) /\ ~patterned
           tag == IF Vendor THEN "decoded"
                  ELSE IF patterned /\ Mutation = "overwrite-pattern"
                       THEN "decoded" ELSE genericData
       IN s' = [s EXCEPT
           !.data[i] = tag,
           !.oob[i] = IF Vendor \/ (OobRequired /\ ~IsError(kind)) THEN tag ELSE @,
           !.cache = IF useCount THEN cache ELSE @,
           !.cacheKey = IF useCount THEN word ELSE @,
           !.corrected = @ + IF useCount THEN count ELSE 0,
           !.maxflips = IF useCount THEN Max(@, count) ELSE @,
           !.errors = IF Vendor /\ (IsError(kind) \/ IsPattern(kind))
                      THEN @ \cup {i} ELSE @,
           !.zeros = IF Vendor /\ IsZero(kind) THEN @ \cup {i} ELSE @,
           !.consumed = IF Vendor \/ ~IsError(kind) THEN @ \cup {i} ELSE @,
           !.captured = @ \cup {i},
           !.erased = @ \/ (~Vendor /\ patterned /\ ones),
           !.slot = @ + 1]
StartRaw ==
    /\ s.pc = "good" /\ s.slot = s.count
    /\ s' = [s EXCEPT !.pc = "raw", !.slot = 0]
Raw ==
    /\ s.pc = "raw" /\ s.slot < s.count
    /\ LET i == s.first + s.slot
           raw == ~Vendor /\ IsError(s.input[i])
           io == raw /\ s.rawInput[i] = "io"
           erased == raw /\ s.rawInput[i] = "erased"
       IN s' = [s EXCEPT
           !.pc = IF io THEN "done" ELSE @,
           !.transportError = @ \/ io,
           !.data[i] = IF raw /\ ~io THEN IF erased THEN "ones" ELSE "physical" ELSE @,
           !.oob[i] = IF raw /\ ~io THEN IF erased THEN "ones" ELSE "physical" ELSE @,
           !.clobbered = @ \/ raw,
           !.consumed = IF raw /\ ~io THEN @ \cup {i} ELSE @,
           !.corrected = @ + IF erased THEN 1 ELSE 0,
           !.failed = @ + IF raw /\ s.rawInput[i] = "bad" THEN 1 ELSE 0,
           !.maxflips = IF erased THEN Max(@, 1) ELSE @,
           !.erased = @ \/ erased, !.slot = @ + 1]
Advance ==
    /\ s.pc = "raw" /\ s.slot = s.count
    /\ s' = [s EXCEPT !.first = @ + s.count, !.clobbered = FALSE,
                     !.pc = IF s.first + s.count = N THEN "finish" ELSE "batch"]
Finish ==
    /\ s.pc = "finish"
    /\ LET zero == Vendor /\ s.zeros = Steps
           erased == Vendor /\ s.errors # {} /\ SpareErased /\
                     (~zero \/ Mutation = "spare-before-zero")
       IN s' = [s EXCEPT
           !.data = IF erased THEN [i \in Steps |-> "ones"]
                    ELSE IF zero THEN [i \in Steps |-> "zero"] ELSE @,
           !.oob = IF erased THEN [i \in Steps |-> "ones"]
                   ELSE IF zero THEN [i \in Steps |-> "zero"] ELSE @,
           !.failed = IF Vendor THEN
                      IF erased THEN 0 ELSE IF zero THEN N ELSE Cardinality(s.errors)
                      ELSE @,
           !.corrected = IF zero \/ erased THEN 0 ELSE @,
           !.maxflips = IF zero \/ erased THEN 0 ELSE @,
           !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == BeginBatch \/ Good \/ StartRaw \/ Raw \/ Advance \/ Finish \/ Done
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
ExpectedFailures ==
    IF Vendor THEN IF AllZero THEN N ELSE IF VendorErased THEN 0
                   ELSE Cardinality({i \in Steps : IsError(s.input[i]) \/ IsPattern(s.input[i])})
    ELSE Cardinality({i \in Steps : IsError(s.input[i]) /\ s.rawInput[i] = "bad"})
CaptureBeforeRaw == s.clobbered => s.captured = s.first..(s.first + s.count - 1)
ResultCorrect == s.pc = "done" /\ ~s.transportError =>
    /\ s.consumed = Steps
    /\ \A i \in Steps : s.data[i] = ExpectedTag(i)
    /\ \A i \in Steps :
         (Vendor \/ OobRequired \/ IsError(s.input[i])) => s.oob[i] = ExpectedTag(i)
StatsCorrect == s.pc = "done" /\ ~s.transportError =>
    /\ s.corrected = Sum([i \in 1..N |-> Correction(i - 1)])
    /\ s.failed = ExpectedFailures
    /\ s.maxflips = CHOOSE k \in {0} \cup {Correction(i) : i \in Steps} :
                        \A j \in Steps : k >= Correction(j)
Terminates == <> (s.pc = "done")
=============================================================================
