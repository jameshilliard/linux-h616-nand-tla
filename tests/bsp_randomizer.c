/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t __u32;
typedef int32_t __s32;
static uint32_t control;
#define NFC_REG_ECC_CTL 0x34
#define NFC_READ_REG(reg) (assert((reg) == 0x34), control)
#define NFC_WRITE_REG(reg, value) do { assert((reg) == 0x34); control = (value); } while (0)

/* @functions@ */

int main(void)
{
	for (uint32_t seed = 0; seed < 32768; seed++) {
		for (uint32_t low = 0; low < 65536; low += 257) {
			control = 0x92340000 | low;
			assert(NFC_SetRandomSeed(seed) == 0);
			assert(control == (seed << 16 | low));
			assert(NFC_RandomEnable() == 0);
			assert(control == (seed << 16 | low | (1 << 9)));
			assert(NFC_RandomDisable() == 0);
			assert(control == (seed << 16 | (low & ~(1 << 9))));
		}
	}
	puts("8388608 BSP register sequences passed");
}
