/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Keep the common MMIO/column shims, but use the actual NAND-core erased check. */
#define nand_check_erased_ecc_chunk discarded_fixture_erased_check
#include "fixtures.h"
#undef nand_check_erased_ecc_chunk
#include <sys/types.h>
#define likely(v) (v)
#define hweight8(v) __builtin_popcount((u8)(v))
#define hweight_long(v) __builtin_popcountl(v)
#define BITS_PER_BYTE 8
#define BITS_PER_LONG (8 * sizeof(long))
#define IS_ALIGNED(v,a) (((v) & ((a) - 1)) == 0)
