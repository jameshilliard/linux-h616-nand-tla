------------------------------ MODULE NandOob ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, Sequences, FiniteSets
CONSTANTS User, Parity, OobSize, DMA, Mutation
N == Len(User)
Steps == 0..(N - 1)
RECURSIVE Sum(_)
Sum(s) == IF Len(s) = 0 THEN 0 ELSE Head(s) + Sum(Tail(s))
Offset(i) == i * Parity + Sum(SubSeq(User, 1, i))
EccOff(i) == Offset(i) + User[i + 1]
Protected == UNION {Offset(i)..(EccOff(i) - 1) : i \in Steps}
ParityBytes == UNION {EccOff(i)..(Offset(i + 1) - 1) : i \in Steps}
TailBytes == Offset(N)..(OobSize - 1)
AllBytes == 0..(OobSize - 1)
Last(i) == CHOOSE j \in i..(N - 1) :
    /\ \A k \in (i + 1)..j : User[k + 1] = 0
    /\ j = N - 1 \/ User[j + 2] # 0

VARIABLES pc, step, memory, rawRead, converted
vars == <<pc, step, memory, rawRead, converted>>
Init ==
    /\ pc = "read" /\ step = 0 /\ rawRead = {} /\ converted = {}
    \* Protected OOB has already been captured from USER_DATA registers.
    /\ memory = [i \in AllBytes |-> IF i \in Protected THEN "decoded" ELSE "old"]
Read ==
    /\ pc = "read" /\ step < N
    /\ LET last == IF Mutation = "merge-protected" THEN N - 1 ELSE Last(step)
           stop == IF last = N - 1 THEN OobSize ELSE Offset(last + 1)
           region == EccOff(step)..(stop - 1)
       IN /\ rawRead' = rawRead \cup region
          /\ memory' = [i \in AllBytes |-> IF i \in region THEN "raw" ELSE memory[i]]
          /\ step' = last + 1
    /\ UNCHANGED <<pc, converted>>
BeginConvert ==
    /\ pc = "read" /\ step = N
    /\ pc' = "convert" /\ step' = 0
    /\ UNCHANGED <<memory, rawRead, converted>>
ConvertParity ==
    /\ pc = "convert" /\ step < N
    /\ LET region == EccOff(step)..(Offset(step + 1) - 1)
       IN /\ memory' = [i \in AllBytes |->
                IF i \in region /\ ~DMA THEN "ecc-seed" ELSE memory[i]]
          /\ converted' = converted \cup region
    /\ step' = step + 1 /\ UNCHANGED <<pc, rawRead>>
ConvertTail ==
    /\ pc = "convert" /\ step = N
    /\ memory' = [i \in AllBytes |->
         IF i \in TailBytes THEN
           IF Mutation = "wrong-tail-seed" THEN "ecc-seed" ELSE "page-seed"
         ELSE memory[i]]
    /\ pc' = "done" /\ UNCHANGED <<step, rawRead, converted>>
Done == pc = "done" /\ UNCHANGED vars
Next == Read \/ BeginConvert \/ ConvertParity \/ ConvertTail \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ProtectedPreserved == \A i \in Protected : memory[i] = "decoded"
ReadBounds == rawRead \subseteq AllBytes \ Protected
Partition == /\ Protected \cap ParityBytes = {}
             /\ (Protected \cup ParityBytes) \cap TailBytes = {}
             /\ Protected \cup ParityBytes \cup TailBytes = AllBytes
Representation == pc = "done" =>
    /\ rawRead = AllBytes \ Protected
    /\ \A i \in ParityBytes : memory[i] = IF DMA THEN "raw" ELSE "ecc-seed"
    /\ \A i \in TailBytes : memory[i] = "page-seed"
Terminates == <> (pc = "done")
=============================================================================
