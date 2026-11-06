------------------------ MODULE NandRandomizerBits ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Numeric recurrence versus independently represented shift-register bits.
EXTENDS NandRandomizerMath
CONSTANTS SeedLo, SeedHi, Count, Mutation
VARIABLE s
vars == <<s>>
Init == \E seed \in SeedLo..SeedHi:
    s = [word |-> IF Mutation = "omit-mask" THEN seed ELSE seed % 32768,
         bits |-> Bits(seed), count |-> 0]
Shift == /\ s.count < Count
    /\ s' = [s EXCEPT !.count = @ + 1, !.bits = Tick(@),
        !.word = IF Mutation = "wrong-tap"
                 THEN @ \div 2 + ((Bit(@, 0) + Bit(@, 2)) % 2) * 16384
                 ELSE Step(@)]
Done == s.count = Count /\ UNCHANGED vars
Next == Shift \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ExactState == s.word = Pack(s.bits, 15)
ByteOrder == (IF Mutation = "reverse-byte"
              THEN Pack([i \in 1..8 |-> Bit(s.word, 8 - i)], 8)
              ELSE s.word % 256) = Pack(s.bits, 8)
Bounds == s.word \in 0..32767 /\ s.count \in 0..Count
Terminates == <> (s.count = Count)
=============================================================================
