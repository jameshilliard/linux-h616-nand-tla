------------------------ MODULE NandRandomizerMath ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Exact 15-bit recurrence. Bit positions are numbered from the low bit.
\* This defines arithmetic, not when a particular NFC reloads its LFSR.
EXTENDS Integers, Sequences
Bit(n, i) == (n \div (2^i)) % 2
Bits(n) == [i \in 1..15 |-> Bit(n, i - 1)]
Tick(b) == [i \in 1..15 |-> IF i = 15 THEN (b[1] + b[2]) % 2
                          ELSE b[i + 1]]
RECURSIVE Pack(_, _)
Pack(b, n) == IF n = 0 THEN 0 ELSE b[n] * 2^(n - 1) + Pack(b, n - 1)
Step(n) == (n % 32768) \div 2 + ((Bit(n, 0) + Bit(n, 1)) % 2) * 16384
RECURSIVE Advance(_, _)
Advance(n, count) == IF count = 0 THEN n % 32768
                    ELSE Step(Advance(n, count - 1))
RECURSIVE BitAdvance(_, _)
BitAdvance(b, count) == IF count = 0 THEN b ELSE Tick(BitAdvance(b, count - 1))
XorByte(a, b) == Pack([i \in 1..8 |-> (Bit(a, i - 1) + Bit(b, i - 1)) % 2], 8)
Mask(seed, n) == [i \in 1..n |-> Advance(seed, 8 * (i - 1)) % 256]
Apply(bytes, mask) == [i \in 1..Len(bytes) |-> XorByte(bytes[i], mask[i])]
=============================================================================
