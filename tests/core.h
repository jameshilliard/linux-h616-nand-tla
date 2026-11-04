/* SPDX-License-Identifier: GPL-2.0-or-later */
/* API fixtures, not replacements for the extracted algorithms.
 * Small geometry makes scans exhaustive. Callbacks emulate NAND contents/ECC,
 * not a BCH encoder, analog cells, a real DMA provider, or continuous reads. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define max_t(t,a,b) max((t)(a),(t)(b))
#define ALIGN(x,a) (((x) + (a) - 1) & ~((a) - 1))
#define IS_ALIGNED(x,a) (!((x) & ((a) - 1)))
#define likely(x) (x)
#define unlikely(x) (x)
#define BUG_ON(x) assert(!(x))
#define BUG() assert(0)
#define WARN_ON_ONCE(x) (!!(x))
#define pr_debug(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_notice(...) ((void)0)
#define pr_err(...) ((void)0)
#define ENOTSUPP EOPNOTSUPP
#define GFP_KERNEL 0
#define kmalloc(n,f) malloc(n)
#define kfree(p) free(p)
#define NOTALIGNED(x) (((x) & (chip->subpagesize - 1)) != 0)
enum { MTD_OPS_PLACE_OOB, MTD_OPS_AUTO_OOB, MTD_OPS_RAW };
struct mtd_oob_ops {
    unsigned int mode;
    size_t len, retlen, ooblen, oobretlen;
    u32 ooboffs;
    u8 *datbuf, *oobbuf;
};
struct mtd_ecc_stats { uint32_t corrected, failed, badblocks, bbtblocks; };
struct mtd_oob_region { u32 offset, length; };
struct mtd_info;
struct mtd_ooblayout_ops { int (*free)(struct mtd_info *, int, struct mtd_oob_region *); };
struct mtd_info {
    u32 writesize, oobsize, oobavail, erasesize;
    u64 size;
    struct mtd_ecc_stats ecc_stats;
    struct mtd_info *parent;
    const struct mtd_ooblayout_ops *ooblayout;
};
/* @bbm@ */
struct erase_info { u64 addr, len; };
struct nand_device { u64 targetsize; unsigned int targets; };
struct nand_chip {
    struct mtd_info mtd;
    struct nand_device base;
    unsigned int options, bbt_options, subpagesize, buf_align, badblockpos;
    int page_shift, chip_shift, bbt_erase_shift, phys_erase_shift, pagemask, read_retries, selected;
    bool subpage_read;
    struct { int page, bitflips; } pagecache;
    struct { bool ongoing; } cont_read;
    u8 *data_buf, *oob_poi, *bbt;
    struct nand_bbt_descr *bbt_td, *bbt_md, *badblock_pattern;
    struct { int (*setup_read_retry)(struct nand_chip *, int); } ops;
    struct { int (*block_markbad)(struct nand_chip *, loff_t); } legacy;
    struct {
        int (*read_page)(struct nand_chip *, u8 *, int, int);
        int (*read_page_raw)(struct nand_chip *, u8 *, int, int);
        int (*read_subpage)(struct nand_chip *, u32, u32, u8 *, int);
        int (*read_oob)(struct nand_chip *, int);
        int (*read_oob_raw)(struct nand_chip *, int);
        int (*write_page)(struct nand_chip *, const u8 *, int, int);
        int (*write_page_raw)(struct nand_chip *, const u8 *, int, int);
        int (*write_subpage)(struct nand_chip *, u32, u32, const u8 *, int, int);
        int (*write_oob)(struct nand_chip *, int);
        int (*write_oob_raw)(struct nand_chip *, int);
    } ecc;
};
#define nand_to_mtd(c) (&(c)->mtd)
#define mtd_to_nand(m) ((struct nand_chip *)(m))
#define nanddev_target_size(b) ((b)->targetsize)
#define nanddev_ntargets(b) ((b)->targets)
#define NAND_HAS_SUBPAGE_READ(c) ((c)->subpage_read)
#define nand_get_data_buf(c) ((c)->data_buf)
static bool secure_region, write_protected;
static bool device_locked;
static int reset_error;
static bool nand_region_is_secured(struct nand_chip *c, loff_t o, size_t n) { return secure_region; }
static bool virt_addr_valid(void *p) { return true; }
static int nand_check_wp(struct nand_chip *c) { return write_protected; }
static int nand_reset(struct nand_chip *c, int target) { return reset_error; }
static void nand_select_target(struct nand_chip *c, int t) { assert(t >= 0 && t < 2); c->selected = t; }
static void nand_deselect_target(struct nand_chip *c) { c->selected = -1; }
static void rawnand_enable_cont_reads(struct nand_chip *c, unsigned int p, u32 n, int col) { c->cont_read.ongoing = false; }
static void rawnand_cont_read_skip_first_page(struct nand_chip *c, unsigned int p) { assert(!c->cont_read.ongoing); }
static void nand_wait_readrdy(struct nand_chip *c) { assert(!(c->options & NAND_NEED_READRDY)); }
static bool mtd_is_eccerr(int r) { return r == -EBADMSG; }
static bool mtd_is_bitflip(int r) { return r == -EUCLEAN; }
static bool mtd_is_bitflip_or_eccerr(int r) { return mtd_is_eccerr(r) || mtd_is_bitflip(r); }
static bool mtd_check_expert_analysis_mode(void) { return false; }
static void nand_get_device(struct nand_chip *c) { assert(!device_locked); device_locked=true; }
static void nand_release_device(struct nand_chip *c) { assert(device_locked); device_locked=false; }
int nand_markbad_bbt(struct nand_chip *, loff_t);
static int mtd_read(struct mtd_info *, loff_t, size_t, size_t *, u8 *);
static int mtd_read_oob(struct mtd_info *, loff_t, struct mtd_oob_ops *);
static int mtd_write_oob(struct mtd_info *, loff_t, struct mtd_oob_ops *);
static int nand_erase_nand(struct nand_chip *, struct erase_info *, int);
