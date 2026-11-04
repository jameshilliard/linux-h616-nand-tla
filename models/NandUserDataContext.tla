------------------------- MODULE NandUserDataContext -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Persistent packed length registers across serialized PIO/DMA requests.
\* Register words are eight nibbles, avoiding TLC 32-bit overflow.
\* Inputs are admitted batches, not a replacement for NandBatch's planner.
EXTENDS Integers, Sequences
CONSTANTS Operations, VariableLengths, Mutation
VARIABLE s
vars == <<s>>
Slots == 0..31
Table == <<0,4,8,12,16,20,24,28,32>>
Code(n) == (CHOOSE i \in 1..Len(Table) : Table[i] = n) - 1
Op == Operations[s.op]
Requested(i) == IF i < Op.count THEN Op.user[Op.first + i + 1] ELSE 0
Init == s = [pc |-> "begin", op |-> 1, word |-> 0,
    regs |-> [i \in Slots |-> 15], writes |-> 0,
    bad |-> FALSE, stale |-> FALSE, bank |-> 0, completed |-> 0]
Begin == /\ s.pc = "begin" /\ s.op <= Len(Operations)
    /\ s' = [s EXCEPT !.word = 0,
        !.pc = IF VariableLengths \/ Mutation = "legacy-write"
               THEN IF Op.dma THEN "program" ELSE "clear" ELSE "consume"]
Clear == /\ s.pc = "clear"
    /\ s' = [s EXCEPT !.regs = [i \in Slots |-> IF i \div 8 = s.word THEN 0 ELSE s.regs[i]],
        !.writes = @ + 1, !.word = @ + 1,
        !.pc = IF s.word = 3 THEN "pio" ELSE "clear"]
Pio == /\ s.pc = "pio"
    /\ s' = [s EXCEPT !.regs = [i \in Slots |->
        IF i = 0 THEN Code(Requested(0)) ELSE s.regs[i]],
        !.writes = @ + 1, !.pc = "consume"]
Program == /\ s.pc = "program"
    /\ LET value(i) == IF Mutation = "wrong-logical-base" /\ i < Op.count
                       THEN Code(Op.user[i + 1]) ELSE Code(Requested(i))
       IN s' = [s EXCEPT !.regs = [i \in Slots |->
           IF i \div 8 # s.word THEN s.regs[i]
           ELSE IF Mutation = "skip-zero" /\ value(i) = 0 THEN s.regs[i]
           ELSE IF Mutation = "skip-unused" /\ i >= Op.count THEN s.regs[i]
           ELSE value(i)],
           !.writes = @ + 1, !.word = @ + 1,
           !.pc = IF s.word = 3 THEN "consume" ELSE "program"]
RECURSIVE Total(_)
Total(n) == IF n = 0 THEN 0 ELSE Requested(n-1) + Total(n-1)
Consume == /\ s.pc = "consume"
    /\ s' = [s EXCEPT
        !.bad = @ \/ (VariableLengths /\ \E i \in 0..(Op.count-1) : s.regs[i] # Code(Requested(i))),
        !.stale = @ \/ (VariableLengths /\ \E i \in Op.count..31 : s.regs[i] # 0),
        !.bank = Total(Op.count), !.completed = @ + 1,
        !.op = @ + 1, !.pc = "begin"]
Finish == /\ s.pc = "begin" /\ s.op > Len(Operations)
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Begin \/ Clear \/ Pio \/ Program \/ Consume \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ActiveLengths == ~s.bad
UnusedCleared == ~s.stale
BankBounds == s.bank <= 128
LegacyNoLengthMMIO == VariableLengths \/ s.writes = 0
Terminates == <> (s.pc = "done")
=============================================================================
