----------------------------- MODULE NandBatch -----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, Sequences, FiniteSets, TLC
CONSTANTS User, Parity, StepSize, OobSize, MaxSteps, VariableLength, DMA,
          Start, Stop, Mutation
N == Len(User)
Steps == 0..(N - 1)
RECURSIVE Sum(_)
Sum(s) == IF Len(s) = 0 THEN 0 ELSE Head(s) + Sum(Tail(s))
Offset(i) == i * Parity + Sum(SubSeq(User, 1, i))
Protected(i) == Offset(i)..(Offset(i) + User[i + 1] - 1)
ParityRegion(i) == (Offset(i) + User[i + 1])..(Offset(i + 1) - 1)
Used == Offset(N)
BankBytes == IF VariableLength THEN 128 ELSE MaxSteps * 4
Fits(first, count) == Sum(SubSeq(User, first + 1, first + count)) <= BankBytes
MaxCount(first) ==
    CHOOSE k \in 1..(Stop - first) :
      Fits(first, k) /\ \A j \in (k + 1)..(Stop - first) : ~Fits(first, j)
Count(first) ==
    IF ~DMA THEN 1
    ELSE IF ~VariableLength \/ Mutation = "unbounded-bank" THEN Stop - first
    ELSE IF first > 0 /\ Mutation # "late-multiple-slots" THEN 1
    ELSE MaxCount(first)

VARIABLES pc, first, count, slot, seen, actualMain, actualOob,
          lengthUsed, codes, accesses, visits
vars == <<pc, first, count, slot, seen, actualMain, actualOob,
          lengthUsed, codes, accesses, visits>>
Init ==
    /\ pc = "plan" /\ first = Start /\ count = 0 /\ slot = 0
    /\ seen = {} /\ actualMain = -1 /\ actualOob = -1 /\ lengthUsed = 0
    /\ codes = [i \in 0..(MaxSteps - 1) |-> 0] /\ accesses = {}
    /\ visits = [i \in Steps |-> 0]

Plan ==
    /\ pc = "plan" /\ first < Stop
    /\ count' = Count(first) /\ slot' = 0 /\ pc' = "consume"
    /\ codes' = [i \in 0..(MaxSteps - 1) |->
         IF i < Count(first) THEN User[first + i + 1] \div 4 ELSE 0]
    /\ UNCHANGED <<first, seen, actualMain, actualOob, lengthUsed, accesses, visits>>
Consume ==
    /\ pc = "consume" /\ slot < count
    /\ LET logical == first + slot
           \* PAGE_OP's later slots use page-relative columns, not a
           \* continuation from a manually selected nonzero first column.
           hwMain == IF ~DMA \/ slot = 0 THEN logical * StepSize
                     ELSE slot * StepSize
           spareBase == IF Mutation = "lost-spare-base" /\ DMA /\ first > 0
                        THEN 0 ELSE Offset(first)
           hwOob == spareBase + slot * Parity +
                    Sum(SubSeq(User, first + 1, logical))
       IN /\ actualMain' = hwMain /\ actualOob' = hwOob
          /\ lengthUsed' = IF Mutation = "pio-slot-length" /\ ~DMA
                           THEN User[1] ELSE User[logical + 1]
          /\ accesses' = accesses \cup
               {<<logical, hwMain, hwOob,
                  IF Mutation = "pio-slot-length" /\ ~DMA
                  THEN User[1] ELSE User[logical + 1]>>}
          /\ seen' = seen \cup {logical}
          /\ visits' = [visits EXCEPT ![logical] = @ + 1]
    /\ slot' = IF Mutation = "duplicate-step" /\ visits[first + slot] = 0
                THEN slot ELSE slot + 1
    /\ UNCHANGED <<pc, first, count, codes>>
Advance ==
    /\ pc = "consume" /\ slot = count
    /\ first' = first + count
    /\ pc' = IF first + count = Stop THEN "done" ELSE "plan"
    /\ UNCHANGED <<count, slot, seen, actualMain, actualOob,
                   lengthUsed, codes, accesses, visits>>
Done == pc = "done" /\ UNCHANGED vars
Next == Plan \/ Consume \/ Advance \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)

Geometry ==
    /\ N \in 1..MaxSteps /\ Start \in Steps /\ Stop \in (Start + 1)..N
    /\ User[1] >= 4 /\ \A i \in 1..N : User[i] \in {0,4,8,12,16,20,24,28,32}
    /\ Used <= OobSize
    /\ \A i \in Steps :
         /\ Protected(i) \cap ParityRegion(i) = {}
         /\ Protected(i) \cup ParityRegion(i) = Offset(i)..(Offset(i + 1) - 1)
         /\ \A j \in Steps \ {i} :
              (Protected(i) \cup ParityRegion(i)) \cap
              (Protected(j) \cup ParityRegion(j)) = {}
RegisterBounds == pc = "consume" =>
    /\ count > 0 /\ count <= MaxSteps /\ first + count <= Stop
    /\ Fits(first, count)
LengthRegisters == pc = "consume" =>
    \A i \in 0..(MaxSteps - 1) :
      codes[i] = IF i < count THEN User[first + i + 1] \div 4 ELSE 0
CorrectColumns == \A a \in accesses :
    a[2] = a[1] * StepSize /\ a[3] = Offset(a[1])
LogicalLength == \A a \in accesses : a[4] = User[a[1] + 1]
ExactlyOnce == /\ \A i \in Steps : visits[i] <= 1
               /\ seen = {i \in Steps : visits[i] = 1}
Complete == pc = "done" => seen = Start..(Stop - 1)
Terminates == <> (pc = "done")
=============================================================================
