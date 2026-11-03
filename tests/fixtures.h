/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host fixtures, not an emulator or replacement for the kernel DMA/MMIO APIs. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BIT(n) (UINT32_C(1) << (n))
#define GENMASK(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min_t(t, a, b) min((t)(a), (t)(b))
#define max_t(t, a, b) max((t)(a), (t)(b))
#define field_get(m, v) (((v) & (m)) >> __builtin_ctz((unsigned)(m)))
#define field_prep(m, v) (((u32)(v) << __builtin_ctz((unsigned)(m))) & (m))
#define FIELD_PREP(m, v) field_prep(m, v)
#define unlikely(v) (v)
#define hweight32(v) __builtin_popcount((u32)(v))
#define USER_DATA_SZ 4
#define SUNXI_NFC_MAX_USER_DATA_SZ 32
#define SUNXI_NFC_H6_ALLWINNER_OOB_MAX_USER_DATA_SZ 16
#define NFC_H6_USER_DATA_REGS 32
#define NFC_REG_USER_DATA_LEN_CAPACITY 8
#define NFC_REG_USER_DATA_LEN(n, s) ((n)->caps->reg_user_data_len + ((s) / 8) * 4)
#define NFC_USER_DATA_LEN_MSK(s) (UINT32_C(15) << (((s) % 8) * 4))
#define NFC_REG_USER_DATA(n, i) ((n)->caps->reg_user_data + (i) * 4)
#define NFC_REG_ECC_ST 0x38
#define NFC_REG_PAT_ID(n) ((n)->caps->reg_pat_id)
#define NFC_REG_ECC_ERR_CNT(n, i) (((n)->caps->reg_ecc_err_cnt + (i)) & ~3U)
#define NFC_ECC_ERR(n) BIT(n)
#define NFC_ECC_ERR_MSK(n) ((n)->caps->ecc_err_mask)
#define NFC_ECC_PAT_FOUND_MSK(n) ((n)->caps->pat_found_mask)
#define NFC_ECC_ERR_CNT(b, x) (((x) >> (((b) % 4) * 8)) & 0xff)
#define NFC_RAM0_BASE 0x400
#define NAND_NEED_SCRAMBLING 1
#define GFP_KERNEL 0
#define dev_warn(...) ((void)0)
#define dev_err(...) ((void)0)
#define ENOTSUPP 524
#define ALIGN(n, a) (((n) + (a) - 1) & ~((a) - 1))
static unsigned fls(unsigned v) { return v ? 32 - __builtin_clz(v) : 0; }
#define NAND_ECC_MAXIMIZE_STRENGTH 1
#define NAND_USES_DMA 2
#define NAND_NO_SUBPAGE_WRITE 4
#define NAND_SUBPAGE_READ 8
#define NAND_BBT_USE_FLASH 1
#define NAND_BBT_NO_OOB 2
#define NAND_ECC_ENGINE_TYPE_ON_HOST 0
#define NAND_ECC_ENGINE_TYPE_NONE 1
#define NAND_ECC_ENGINE_TYPE_SOFT 2
#define NAND_ECC_ENGINE_TYPE_ON_DIE 3
#define dev_err_probe(dev, err, ...) ((void)(dev), (err))
#define NFC_ECC_MODE(n, m) field_prep((n)->caps->ecc_mode_mask, (m))
#define NFC_ECC_EXCEPTION BIT(4)
#define NFC_ECC_PIPELINE BIT(3)
#define NFC_ECC_EN BIT(0)
#define NFC_ECC_BLOCK_512 BIT(5)
#define mtd_set_ooblayout(m, o) ((void)0)
#define mtd_div_by_ws(e, m) ((e) / (m)->writesize)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define NFC_REG_CMD 0x24
#define NFC_REG_RCMD_SET 0x28
#define NFC_REG_WCMD_SET 0x2c
#define NFC_REG_SPARE_AREA(n) ((n)->caps->reg_spare_area)
#define NFC_CMD_INT_FLAG BIT(1)
#define NFC_DMA_INT_FLAG BIT(2)
#define NFC_ACCESS_DIR BIT(20)
#define NFC_DATA_TRANS BIT(21)
#define NFC_DATA_SWAP_METHOD BIT(26)
#define NFC_ECC_OP (1U << 30)
#define NFC_PAGE_OP (2U << 30)
#define NAND_CMD_READSTART 0x30
#define NAND_CMD_RNDOUT 0x05
#define NAND_CMD_RNDOUTSTART 0xe0
#define NAND_CMD_RNDIN 0x85
#define NAND_CMD_PAGEPROG 0x10
#define DMA_FROM_DEVICE 0
#define DMA_TO_DEVICE 1

struct nand_chip;
struct nand_ecc_ctrl {
    int size, bytes, steps, strength;
    int engine_type;
    u32 ecc_ctl;
    int (*write_page)(struct nand_chip *, const u8 *, int, int);
    int (*read_page)(struct nand_chip *, u8 *, int, int);
    int (*read_subpage)(struct nand_chip *, u32, u32, u8 *, int);
    int (*write_subpage)(struct nand_chip *, u32, u32, const u8 *, int, int);
    int (*read_oob)(struct nand_chip *, int);
    int (*write_oob)(struct nand_chip *, int);
    int (*read_oob_raw)(struct nand_chip *, int);
    int (*write_oob_raw)(struct nand_chip *, int);
};
struct mtd_info {
    unsigned writesize, oobsize, erasesize, ecc_step_size;
    struct { unsigned corrected, failed; } ecc_stats;
};
struct nand_chip;
#define sunxi_nfc_caps caps
struct caps {
    unsigned reg_user_data_len, reg_user_data, reg_pat_id, reg_pat_found;
    unsigned reg_ecc_err_cnt, ecc_err_mask, pat_found_mask, max_ecc_steps;
    const u8 *user_data_len_tab;
    unsigned nuser_data_tab;
    unsigned reg_spare_area;
    const u8 *ecc_strengths;
    unsigned nstrengths, ecc_mode_mask;
    bool has_ecc_block_512;
    bool legacy_max_strength;
    bool (*spare_is_erased)(struct nand_chip *, const u8 *, int);
};
struct sunxi_nfc {
    u8 regs[0x1000];
    struct caps *caps;
    void *dev;
    void *dmac;
    bool use_mdma;
    void *mdma_desc;
};
struct sunxi_nfc_oob_ops;
struct nand_ecc_props { unsigned step_size, strength; };
struct nand_device { struct { struct { unsigned flags; } user_conf; } ecc;
                     struct nand_ecc_props requirements; };
struct nand_chip {
    struct nand_ecc_ctrl ecc;
    struct mtd_info mtd;
    struct sunxi_nfc *controller;
    u8 *oob_poi, *user_data_bytes;
    unsigned options;
    unsigned bbt_options;
    struct nand_device base;
    unsigned cur_cs;
    unsigned buf_align;
    const struct sunxi_nfc_oob_ops *oob_ops;
};
struct mtd_oob_region { unsigned offset, length; };
struct device_node { int unused; };
static struct nand_device device_config;
static bool vendor_property, allocation_failure;
static const struct nand_ecc_props *nanddev_get_ecc_requirements(const struct nand_device *n)
{ return &n->requirements; }
static struct device_node *nand_get_flash_node(struct nand_chip *n) { return NULL; }
static bool of_property_read_bool(struct device_node *np, const char *name)
{ assert(!strcmp(name,"allwinner,randomized-oob")); return vendor_property; }
#define mtd_to_nanddev(m) (&device_config)
#define sunxi_nand_chip nand_chip
#define to_sunxi_nand(n) (n)
#define to_sunxi_nfc(n) (n)
#define nand_to_mtd(n) (&(n)->mtd)
#define mtd_to_nand(m) ((struct nand_chip *)((char *)(m) - offsetof(struct nand_chip, mtd)))

static struct sunxi_nfc controller;
static u8 main_data[32768 + 64], oob_data[8192 + 64];
static unsigned mmio_reads[0x1000 / 4], mmio_writes[0x1000 / 4];
static unsigned raw_calls, fail_raw_at, erased_flips, column, page_reads, column_reads;
static unsigned plain_read_calls, random_read_calls;
static bool raw_is_erased;
/* Ordered fault points in extracted page/control functions. These fixtures
 * assume successful DMA termination; NandTransfer checks that assumption. */
enum flow_point { FP_NONE, FP_FIFO, FP_PREPARE, FP_BEGIN, FP_COLUMN,
                  FP_BUFFER, FP_WAIT, FP_CONFIRM, FP_READ };
static struct {
    bool enabled, mapped, active, ecc, random;
    unsigned calls, fail_at, begins, confirms, issued, dma_issued, prepares, aborts;
    unsigned issued_at_failure;
    enum flow_point failure;
} flow;
struct scatterlist { unsigned len; };
static int flow_event(enum flow_point point) {
    if (flow.enabled && ++flow.calls == flow.fail_at) {
        flow.failure = point;
        flow.issued_at_failure = flow.dma_issued;
        return -ETIMEDOUT;
    }
    return 0;
}
static void *devm_kzalloc(void *dev, size_t n, unsigned flags)
{ return allocation_failure ? NULL : calloc(1, n); }
static const void *memchr_inv(const void *data, int value, size_t n) {
    const u8 *p = data;
    for (size_t i = 0; i < n; i++) if (p[i] != (u8)value) return p + i;
    return NULL;
}
static u32 readl(const void *p) {
    ptrdiff_t off = (const u8 *)p - controller.regs;
    u32 v;
    assert(off >= 0 && off + 4 <= (ptrdiff_t)sizeof(controller.regs) && !(off % 4));
    mmio_reads[off / 4]++;
    memcpy(&v, p, 4);
    return v;
}
static void writel(u32 v, void *p) {
    ptrdiff_t off = (u8 *)p - controller.regs;
    assert(off >= 0 && off + 4 <= (ptrdiff_t)sizeof(controller.regs) && !(off % 4));
    mmio_writes[off / 4]++;
    if (flow.enabled && off == NFC_REG_CMD) {
        assert(flow.ecc && flow.random);
        flow.issued++;
        flow.active = flow.mapped;
        if (flow.mapped) flow.dma_issued++;
    }
    memcpy(p, &v, 4);
}
static void memcpy_fromio(void *dst, const void *src, size_t n) {
    ptrdiff_t off = (const u8 *)src - controller.regs;
    assert(off >= 0 && off + n <= sizeof(controller.regs));
    memcpy(dst, src, n);
}
static int transfer(struct nand_chip *n, unsigned col, void *dst, unsigned len) {
    assert(col + len <= n->mtd.writesize + n->mtd.oobsize);
    if (flow_event(FP_READ)) return -ETIMEDOUT;
    if (++raw_calls == fail_raw_at) return -ETIMEDOUT;
    if (dst) memset(dst, 0xa7, len);
    column = col + len;
    return 0;
}
static int nand_read_page_op(struct nand_chip *n, unsigned page,
                            unsigned col, void *dst, unsigned len) {
    page_reads++;
    return transfer(n, col, dst, len);
}
static int nand_change_read_column_op(struct nand_chip *n, unsigned col,
                                     void *dst, unsigned len, bool force) {
    column_reads++;
    assert(n->mtd.writesize > 512);
    return transfer(n, col, dst, len);
}
static int sunxi_nfc_read_buf(struct nand_chip *n, u8 *dst, int len) {
    plain_read_calls++;
    return transfer(n, column, dst, len);
}
static int sunxi_nfc_randomizer_read_buf(struct nand_chip *n, u8 *dst, int len,
                                       bool ecc, int page) {
    random_read_calls++;
    return transfer(n, column, dst, len);
}
static int nand_check_erased_ecc_chunk(void *data, int len, void *oob, int olen,
                                     void *extra, int elen, int strength) {
    /* Error rereads may disturb USER_DATA; all normal steps must be captured first. */
    memset(controller.regs + controller.caps->reg_user_data, 0xee, 128);
    if (!raw_is_erased) return -EBADMSG;
    assert(erased_flips <= (unsigned)strength);
    memset(data, 0xff, len);
    memset(oob, 0xff, olen);
    return erased_flips;
}
static int sunxi_nfc_wait_cmd_fifo_empty(struct sunxi_nfc *n) {
    return flow_event(FP_FIFO);
}
static int sunxi_nfc_wait_events(struct sunxi_nfc *n, u32 events, bool poll, int ms) {
    assert(events == NFC_CMD_INT_FLAG || events == (NFC_CMD_INT_FLAG | NFC_DMA_INT_FLAG));
    if (flow_event(FP_WAIT)) return -ETIMEDOUT;
    flow.active = false;
    return 0;
}
static int sunxi_nfc_dma_op_prepare(struct sunxi_nfc *n, const void *buf,
                                  unsigned size, unsigned chunks, int direction,
                                  struct scatterlist *sg) {
    assert(!flow.mapped && !flow.active);
    flow.prepares++;
    if (flow_event(FP_PREPARE)) return -ETIMEDOUT;
    sg->len = size * chunks;
    assert(sg->len > 0 && sg->len <= UINT16_MAX);
    flow.mapped = true;
    return 0;
}
static void sunxi_nfc_dma_op_abort(struct sunxi_nfc *n) {
    flow.aborts++;
    flow.active = false;
}
static void sunxi_nfc_dma_op_cleanup(struct sunxi_nfc *n, int dir,
                                   struct scatterlist *sg) {
    assert(flow.mapped && !flow.active);
    flow.mapped = false;
}
static void dma_async_issue_pending(void *dmac) { assert(flow.mapped); }
static void sunxi_nfc_select_chip(struct nand_chip *n, unsigned cs) { }
static void sunxi_nfc_hw_ecc_enable(struct nand_chip *n) {
    assert(!flow.ecc);
    flow.ecc = true;
}
static void sunxi_nfc_hw_ecc_disable(struct nand_chip *n) { flow.ecc = false; }
static void sunxi_nfc_randomizer_config(struct nand_chip *n, int page, bool ecc) { }
static void sunxi_nfc_randomizer_enable(struct nand_chip *n) { flow.random = true; }
static void sunxi_nfc_randomizer_disable(struct nand_chip *n) { flow.random = false; }
static int nand_prog_page_begin_op(struct nand_chip *n, int page, unsigned col,
                                   const void *data, unsigned len) {
    flow.begins++;
    assert(flow.begins == 1 && col == 0 && len == 0);
    column = col;
    return flow_event(FP_BEGIN);
}
static int nand_change_write_column_op(struct nand_chip *n, unsigned col,
                                      const void *data, unsigned len, bool force) {
    assert(col + len <= n->mtd.writesize + n->mtd.oobsize);
    if (flow_event(FP_COLUMN)) return -ETIMEDOUT;
    column = col + len;
    return 0;
}
static int sunxi_nfc_randomizer_write_buf(struct nand_chip *n, const u8 *buf,
                                         int len, bool ecc, int page) {
    assert(column + len <= n->mtd.writesize + n->mtd.oobsize);
    if (flow_event(FP_BUFFER)) return -ETIMEDOUT;
    column += len;
    return 0;
}
static int nand_prog_page_end_op(struct nand_chip *n) {
    assert(!flow.ecc && !flow.random && !flow.mapped && !flow.active);
    flow.confirms++;
    assert(flow.confirms == 1);
    return flow_event(FP_CONFIRM);
}
static u8 *nand_get_data_buf(struct nand_chip *n) { return main_data; }
static int nand_read_oob_std(struct nand_chip *n, int page) { return 0; }
static int nand_write_oob_std(struct nand_chip *n, int page) { return 0; }
