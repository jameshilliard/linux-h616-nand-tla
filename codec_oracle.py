# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent, deliberately slow GF(2^14) polynomial-division encoder.

No Linux remainder tables, BCH encoder routines or decoder are reused here.
The field polynomial and LSB-first byte convention come from sunxi-tools.
"""
from functools import lru_cache

REVERSE = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))


@lru_cache(None)
def generator(strength):
    n, primitive = 16383, 0x5803
    exp, log, v = [], [0] * 16384, 1
    for i in range(n):
        exp.append(v)
        log[v] = i
        v <<= 1
        if v & 16384:
            v ^= primitive
    assert len(set(exp)) == n and v == 1

    def multiply(a, b):
        return exp[(log[a] + log[b]) % n] if a and b else 0

    roots = set()
    for i in range(1, strength * 2 + 1):
        root = i
        while root not in roots:
            roots.add(root)
            root = root * 2 % n
    coefficients = [1]
    for root in sorted(roots):
        result = [0] * (len(coefficients) + 1)
        for i, coefficient in enumerate(coefficients):
            result[i] ^= multiply(coefficient, exp[root])
            result[i + 1] ^= coefficient
        coefficients = result
    assert all(v in (0, 1) for v in coefficients)
    polynomial = sum(v << i for i, v in enumerate(coefficients))
    return polynomial, len(coefficients) - 1


def encode(data, strength):
    polynomial, degree = generator(strength)
    dividend = int.from_bytes(data.translate(REVERSE), "big") << degree
    while dividend.bit_length() > degree:
        dividend ^= polynomial << (dividend.bit_length() - degree - 1)
    size = (14 * strength + 7) // 8
    return (dividend << (size * 8 - degree)).to_bytes(size, "big").translate(REVERSE)


def scramble(data, seed):
    # Bit-by-bit implementation, independent from the driver's word recurrence.
    state = seed & 32767
    for _ in range(15):
        state = (state >> 1) | (((state ^ (state >> 1)) & 1) << 14)
    out = bytearray()
    for byte in data:
        mask = 0
        for bit in range(8):
            mask |= (state & 1) << bit
            state = (state >> 1) | (((state ^ (state >> 1)) & 1) << 14)
        out.append(byte ^ mask)
    return bytes(out)
