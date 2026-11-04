--------------------------- MODULE NandOobPipeline ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Bounded source-observation checker. Hardware-decoded bytes and raw media are
\* separate inputs. Bit-exact LFSR/BCH checks live in independent host oracles;
\* keystream inputs here do not prove the controller implements that stream.
EXTENDS Integers, Sequences, FiniteSets, TLC, TLCExt
CONSTANT Traces
VARIABLE s
vars == <<s>>
RECURSIVE Sum(_), Xor(_, _)
Sum(xs) == IF Len(xs) = 0 THEN 0 ELSE Head(xs) + Sum(Tail(xs))
Xor(a, b) == IF a = 0 /\ b = 0 THEN 0
             ELSE (((a % 2) + (b % 2)) % 2) + 2 * Xor(a \div 2, b \div 2)
MaxSet(xs) == CHOOSE v \in xs : \A w \in xs : v >= w
\* These caches contain only immutable, input-derived values. TLCCache is
\* mathematically the identity operator, not an additional model assumption.
InputFor(t) == TLCCache(Traces[t], t)
T == InputFor(s.t)
N == Len(T.user)
Bytes == 1..Len(T.raw)
RECURSIVE Positions(_, _, _)
Positions(user, parity, k) == IF k > Len(user) THEN <<>> ELSE
    LET off == (k-1)*parity + Sum(SubSeq(user,1,k-1))
    IN [j \in 1..user[k] |-> off+j] \o Positions(user,parity,k+1)
GeometryFor(t) == TLCCache(
    LET input == InputFor(t)
        count == Len(input.user)
        offsets == [k \in 1..(count+1) |->
            (k-1)*input.parity + Sum(SubSeq(input.user,1,k-1))]
    IN [offsets |-> offsets, positions |-> Positions(input.user,input.parity,1),
        protected |-> UNION {(offsets[k]+1)..(offsets[k]+input.user[k]) : k \in 1..count},
        owners |-> [i \in 1..offsets[count+1] |->
            CHOOSE k \in 1..count : i \in (offsets[k]+1)..offsets[k+1]]], t)
Geometry == s.geometry
Off(k) == Geometry.offsets[k]
End == Off(N + 1)
Protected == Geometry.protected
Owner(i) == Geometry.owners[i]
Prefix == [j \in 1..10 |->
    IF j > Len(Geometry.positions) THEN 255 ELSE
    LET i == Geometry.positions[j]
    IN Xor(T.decoded[i], T.eccMask[i-Off(Owner(i))])]
Erased == IF T.variable THEN
    Prefix[1] = 255 /\ Cardinality({i \in 1..10 : Prefix[i] = 255}) >= 9
    ELSE (\A i \in 1..8 : Prefix[i] = 255) \/
         (T.step = 1024 /\ (T.page % T.ppb = 0 \/ (T.page % T.ppb) % 128 = 127) /\
          \A i \in 1..4 : Prefix[i] = 255)
Exceptions == {i \in 1..N : T.kinds[i] # 0}
Zeros == {i \in 1..N : T.kinds[i] \in {1, 4}}
Corrected == Sum([i \in 1..N |-> IF i \in Exceptions THEN 0 ELSE i])
Maximum == MaxSet({0} \cup ((1..N) \ Exceptions))
Class == IF Zeros = 1..N THEN "zero"
         ELSE IF Exceptions # {} /\ Erased THEN "erased" ELSE "decoded"

Init == \E t \in 1..Len(Traces) :
    s = [t |-> t, geometry |-> GeometryFor(t),
         phase |-> "batch", batch |-> 1, captured |-> 0,
         memory |-> [i \in 1..Len(Traces[t].raw) |-> 165], cursor |-> 1,
         reads |-> 0, result |-> 0, corrected |-> 7, failed |-> 3,
         batchesMatch |-> TRUE, captureMatch |-> TRUE, classifyMatch |-> TRUE,
         transportMatch |-> TRUE, outputMatch |-> TRUE, statsMatch |-> TRUE,
         returnMatch |-> TRUE, prefixSafe |-> TRUE]
Batch ==
    /\ s.phase = "batch" /\ s.batch <= Len(T.observed.batches)
    /\ LET b == T.observed.batches[s.batch]
           last == b.first + b.count
           captured == s.captured + b.count
           active == (s.captured + 1)..captured
           memory == [i \in Bytes |->
               IF i \in Protected /\ Owner(i) \in active THEN T.decoded[i] ELSE s.memory[i]]
           expectedErrors == [i \in 1..N |-> IF i <= captured /\ i \in Exceptions THEN 1 ELSE 0]
           expectedZeros == [i \in 1..N |-> IF i <= captured /\ i \in Zeros THEN 1 ELSE 0]
       IN s' = [s EXCEPT !.memory = memory, !.captured = captured,
           !.batch = @ + 1,
           !.batchesMatch = @ /\ b.first = s.captured /\ b.count > 0 /\ last <= N /\
               b.errors = expectedErrors /\ b.zeros = expectedZeros /\
               b.corrected = Sum([i \in 1..captured |-> IF i \in Exceptions THEN 0 ELSE i]) /\
               b.maximum = MaxSet({0} \cup ((1..captured) \ Exceptions)) /\ b.global = <<7,3>> /\
               (T.dma \/ b.count = 1)]
Capture ==
    /\ s.phase = "batch" /\ s.batch > Len(T.observed.batches)
    /\ s' = [s EXCEPT !.captureMatch = @ /\ s.captured = N /\ s.memory = T.observed.captured,
                      !.phase = "classify"]
Classify ==
    /\ s.phase = "classify"
    /\ s' = [s EXCEPT !.classifyMatch = @ /\ T.observed.erased = Erased,
        !.memory = CASE Class = "zero" -> [i \in Bytes |-> 0]
                    [] Class = "erased" -> [i \in Bytes |-> 255] [] OTHER -> @,
        !.failed = IF Class = "zero" THEN @ + N ELSE @,
        !.phase = IF Class # "decoded" THEN "finish"
                  ELSE IF T.want THEN "read" ELSE "account"]
Pending == TLCCache(TLCEval((s.cursor..Len(T.raw)) \ Protected), <<s.t,s.cursor>>)
NextRaw == TLCCache(TLCEval(
    LET start == CHOOSE i \in Pending : \A j \in Pending : i <= j
        nextProtected == {i \in Protected : i > start} \cup {Len(T.raw)+1}
        stop == CHOOSE i \in nextProtected : \A j \in nextProtected : i <= j
    IN <<start,stop>>), <<s.t,s.cursor>>)
Read ==
    /\ s.phase = "read" /\ Pending # {}
    \* Bind the region once; lazy re-evaluation inside the byte update is costly.
    /\ \E region \in {NextRaw} :
       LET start == region[1]
           stop == region[2]
           index == s.reads + 1
           event == IF index <= Len(T.observed.reads) THEN T.observed.reads[index]
                    ELSE [off |-> -1, dest |-> -1, len |-> -1, ret |-> 0]
           failed == index = T.fail
           memory == [i \in Bytes |-> IF ~failed /\ i \in start..(stop-1)
                                     THEN T.raw[i] ELSE s.memory[i]]
       IN s' = [s EXCEPT !.reads = index, !.cursor = stop, !.memory = memory,
           !.transportMatch = @ /\ index <= Len(T.observed.reads) /\
               event.off = start-1 /\ event.dest = start-1 /\ event.len = stop-start /\
               event.ret = (IF failed THEN -110 ELSE 0),
           !.prefixSafe = @ /\ (\A i \in Protected : memory[i] = T.decoded[i]),
           !.result = IF failed THEN -110 ELSE @,
           !.phase = IF failed THEN "finish" ELSE @]
Convert ==
    /\ s.phase = "read" /\ Pending = {}
    /\ s' = [s EXCEPT !.memory = [i \in Bytes |->
        IF i > End THEN Xor(s.memory[i],T.tailMask[i-End])
        ELSE IF i \notin Protected /\ ~T.dma THEN Xor(s.memory[i],T.parityMask[i])
        ELSE s.memory[i]], !.phase = "account"]
Account ==
    /\ s.phase = "account"
    /\ s' = [s EXCEPT !.corrected = @ + Corrected, !.failed = @ + Cardinality(Exceptions),
                      !.result = Maximum, !.phase = "finish"]
Finish ==
    /\ s.phase = "finish"
    /\ s' = [s EXCEPT
        !.outputMatch = @ /\ T.observed.oob = s.memory /\ T.observed.guards /\
            T.observed.main = <<Class = "decoded", Class = "zero", Class = "erased">>,
        !.statsMatch = @ /\ T.observed.stats = <<s.corrected,s.failed>>,
        !.returnMatch = @ /\ T.observed.ret = s.result,
        !.transportMatch = @ /\ s.reads = Len(T.observed.reads), !.phase = "done"]
Done == s.phase = "done" /\ UNCHANGED vars
Next == Batch \/ Capture \/ Classify \/ Read \/ Convert \/ Account \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
BatchesMatch == s.batchesMatch
CaptureMatches == s.captureMatch
ClassificationMatches == s.classifyMatch
TransportMatches == s.transportMatch
ProtectedPreserved == s.prefixSafe
OutputMatches == s.outputMatch
AccountingMatches == s.statsMatch
ReturnMatches == s.returnMatch
Terminates == <> (s.phase = "done")
=============================================================================
