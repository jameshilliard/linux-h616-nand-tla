/* SPDX-License-Identifier: GPL-2.0-only */
/* Host fixture. Extracted production functions are inserted at markers. */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#define __user
#include "mtd-abi.h"

typedef uint32_t u32;
typedef uint64_t u64;
#define noinline_for_stack
#define GFP_KERNEL 0
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define max(a, b) ((a) > (b) ? (a) : (b))
/* Same arbitrary-multiple rounding as include/linux/math.h. */
#define rounddown(x, y) ({ typeof(x) __x = (x); __x - (__x % (y)); })
#define check_add_overflow(a, b, p) __builtin_add_overflow((a), (b), (p))
#define kvmalloc(n, flags) malloc(n)
#define kmalloc(n, flags) malloc(n)
#define kvfree(p) free(p)
#define kfree(p) free(p)

static int user_copy(void *dst, const void *src, size_t len)
{
	if (len)
		memcpy(dst, src, len);
	return 0;
}
#define copy_from_user user_copy
#define copy_to_user user_copy
#define put_user(value, ptr) (*(ptr) = (value), 0)

struct mtd_req_stats {
	unsigned int uncorrectable_errors, corrected_bitflips, max_bitflips;
};
struct mtd_oob_ops {
	unsigned int mode;
	size_t len, retlen, ooblen, oobretlen;
	uint32_t ooboffs;
	uint8_t *datbuf, *oobbuf;
	struct mtd_req_stats *stats;
};
struct mtd_info {
	u64 size;
	u32 writesize, erasesize, oobsize, oobavail;
	int (*_read_oob)(struct mtd_info *, loff_t, struct mtd_oob_ops *);
	int (*_read)(struct mtd_info *, loff_t, size_t, size_t *, unsigned char *);
	int (*_write_oob)(struct mtd_info *, loff_t, struct mtd_oob_ops *);
	unsigned int flags, ecc_strength, bitflip_threshold;
	struct mtd_ecc_stats ecc_stats;
};
struct mtd_file_info { unsigned int mode; };
struct file { void *private_data; };
static struct mtd_info *mtd_get_master(struct mtd_info *mtd) { return mtd; }
static u32 mtd_div_by_ws(u64 n, struct mtd_info *mtd) { return n / mtd->writesize; }
static u32 mtd_mod_by_ws(u64 n, struct mtd_info *mtd) { return n % mtd->writesize; }
#define DIV_ROUND_UP_ULL(n, d) (((uint64_t)(n) + (d) - 1) / (d))
static u32 mtd_oobavail(struct mtd_info *mtd, struct mtd_oob_ops *ops)
{
	return ops->mode == MTD_OPS_AUTO_OOB ? mtd->oobavail : mtd->oobsize;
}
static bool mtd_is_bitflip(int ret) { return ret == -EUCLEAN; }
static bool mtd_is_bitflip_or_eccerr(int ret) { return ret == -EUCLEAN || ret == -EBADMSG; }
static int mtd_read_oob(struct mtd_info *, loff_t, struct mtd_oob_ops *);
static int mtd_write_oob(struct mtd_info *, loff_t, struct mtd_oob_ops *);

/* SPDX-License-Identifier: GPL-2.0-only */
static unsigned int alloc_calls, live_allocs, fail_alloc, copy_calls, fail_copy;

static void *chunk_alloc(size_t len)
{
	if (++alloc_calls == fail_alloc)
		return NULL;
	void *p = malloc(len);
	if (p)
		live_allocs++;
	return p;
}

static void chunk_free(void *p)
{
	if (p) {
		assert(live_allocs);
		live_allocs--;
	}
	free(p);
}

static int chunk_copy(void *to, const void *from, size_t len)
{
	if (++copy_calls == fail_copy)
		return 1;
	return user_copy(to, from, len);
}

#undef kvmalloc
#undef kvfree
#undef copy_from_user
#undef copy_to_user
#define kvmalloc(n, flags) chunk_alloc(n)
#define kvfree(p) chunk_free(p)
#define copy_from_user chunk_copy
#define copy_to_user chunk_copy
#ifndef CHECK_TAIL_CONTROL
#define CHECK_TAIL_CONTROL 0
#endif
