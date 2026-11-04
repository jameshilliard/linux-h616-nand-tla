/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NFC_PROBE_H
#define NFC_PROBE_H
#include <linux/types.h>
#include <linux/ioctl.h>

enum { PROBE_INFO, PROBE_READ, PROBE_COUNT, PROBE_BANKS,
       PROBE_ERASE, PROBE_PROGRAM, PROBE_ECC_READ, PROBE_ECC_WRITE,
       PROBE_PIO_ECC_READ, PROBE_EVENTS };
#define PROBE_RANDOM 1
#define PROBE_RESEED 2
#define PROBE_PAGE_RANDOM 4
#define PROBE_PIPELINE 8
#define PROBE_WORD_READ 16
#define PROBE_RAW_SIZE 2176
struct probe_request {
	__u32 op, page, column, length, chunk, seed, flags;
	__s32 result;
	__u32 regs[16];
	__u8 data[PROBE_RAW_SIZE];
};
#define PROBE_IOC _IOWR('N', 0x61, struct probe_request)
#endif
