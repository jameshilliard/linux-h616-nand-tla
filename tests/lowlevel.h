/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Kernel API boundary fixtures. No physical device or kernel memory is used. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <endian.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int32_t s32;
typedef uint32_t __le32;
typedef uint64_t dma_addr_t;
typedef int dma_cookie_t;
typedef int irqreturn_t;
#define BIT(n) (UINT32_C(1) << (n))
#define GENMASK(h,l) ((UINT32_MAX >> (31-(h))) & (UINT32_MAX << (l)))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define IS_ALIGNED(n,a) (!((n) & ((a)-1)))
#define cpu_to_le32(n) htole32((u32)(n))
#define WARN_ON(n) (!!(n))
#define dev_err(...) ((void)0)
#define dev_warn(...) ((void)0)
#define ENOTSUPP 524
#define IS_ERR(p) ((intptr_t)(p) < 0 && (intptr_t)(p) >= -4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define DMA_BIT_MASK(n) ((UINT64_C(1)<<(n))-1)
#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define DMA_SLAVE_BUSWIDTH_4_BYTES 4
#define NSEC_PER_SEC 1000000000UL
#define NAND_DATA_IFACE_CHECK_ONLY -1
#define NAND_NEED_SCRAMBLING 1
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define DMA_CTRL_ACK 1
#define msecs_to_jiffies(n) (n)
enum nand_op_instr_type { NAND_OP_CMD_INSTR, NAND_OP_ADDR_INSTR,
    NAND_OP_DATA_IN_INSTR, NAND_OP_DATA_OUT_INSTR, NAND_OP_WAITRDY_INSTR };
#define IS_ENABLED(n) 0
#define EXPORT_SYMBOL_GPL(n)
#define pr_debug(...) ((void)0)
enum dma_data_direction { DMA_FROM_DEVICE, DMA_TO_DEVICE };
enum dma_transfer_direction { DMA_DEV_TO_MEM, DMA_MEM_TO_DEV };
struct scatterlist { unsigned len; dma_addr_t address; };
struct dma_async_tx_descriptor { int unused; };
struct completion { bool done; };
struct sunxi_nfc_timings { u8 setup_cycles; s32 tWB[4], tADL[4], tWHR[4], tRHW[4]; };
struct sunxi_nfc_caps {
    bool has_mdma, has_mdma_desc;
    unsigned reg_spare_area, random_en_mask;
    unsigned reg_io_data, dma_maxburst;
    int sram_size;
    const struct sunxi_nfc_timings *timings;
};
struct sunxi_nfc_mdma_desc { __le32 config, size, buf, next; };
struct sunxi_nfc {
    void *regs, *dev, *dmac, *mod_clk;
    struct sunxi_nfc_caps *caps;
    bool use_mdma;
    struct sunxi_nfc_mdma_desc *mdma_desc;
    dma_addr_t mdma_desc_dma;
    unsigned long clk_rate;
    int irq;
    struct completion complete;
};
struct mtd_info { unsigned writesize, erasesize, ecc_step_size; };
struct nand_chip {
    struct sunxi_nfc *controller;
    struct mtd_info mtd;
    unsigned page_shift, options;
};
struct sunxi_nand_chip_sel { u8 cs; s8 rb; };
struct sunxi_nand_chip {
    struct nand_chip nand;
    struct { u32 ecc_ctl; } ecc;
    unsigned long clk_rate;
    u32 timing_cfg, timing_ctl;
    unsigned nsels;
    struct sunxi_nand_chip_sel sels[2];
};
#define to_sunxi_nand(n) ((struct sunxi_nand_chip *)(n))
#define to_sunxi_nfc(n) (n)
#define nand_to_mtd(n) (&(n)->mtd)
#define mtd_div_by_ws(n,m) ((n)/(m)->writesize)
struct nand_op_instr {
    unsigned type;
    union {
        struct { u8 opcode; } cmd;
        struct { const u8 *addrs; unsigned naddrs; } addr;
        struct { union { void *in; const void *out; } buf; unsigned len; } data;
        struct { unsigned timeout_ms; } waitrdy;
    } ctx;
};
/* @parser-header@ */
struct nand_sdr_timings {
    u32 tCLS_min, tCLH_min, tCS_min, tCH_min, tWP_min, tWH_min, tALS_min,
        tDS_min, tDH_min, tRR_min, tALH_min, tRP_min, tREH_min, tRC_min,
        tWC_min, tWB_max, tADL_min, tWHR_min, tRHW_min, tREA_max, tRLOH_min;
};
struct nand_interface_config { struct nand_sdr_timings sdr; bool invalid; };
static const struct nand_sdr_timings *nand_get_sdr_timings(const struct nand_interface_config *c)
{ return c->invalid ? (const void *)(intptr_t)-EINVAL : &c->sdr; }

static struct sunxi_nfc nfc;
static struct sunxi_nfc_caps caps;
static struct sunxi_nfc_mdma_desc desc;
static struct sunxi_nand_chip chip;
static u8 registers[0x3000], media[2][20000];
static struct {
    unsigned maps, unmaps, prep, submit, term, barrier, doorbell, serial;
    unsigned commands, reads, writes, row, column, fail_command, partial_bytes, address_cycles;
    unsigned disable, enable, wait_calls, irq_calls, poll_calls;
    enum dma_data_direction map_direction, unmap_direction;
    enum dma_transfer_direction prep_direction;
    bool map_fail, prep_fail, submit_fail, reset_fail, terminate_fail;
    bool mapped, active, irq_disabled, fifo_busy, published, unsafe;
    bool mask_fail, alloc_fail;
    int channel_error, config_error;
    u64 dma_mask;
    int wait_mode, clock_set_error;
    dma_addr_t map_address;
    long rounded_rate;
    unsigned long real_rate, requested_rate;
    unsigned clock_calls, soft_wait_ms;
} hw;
static struct dma_async_tx_descriptor descriptor;
static irqreturn_t sunxi_nfc_interrupt(int, void *);
static u32 readl(const void *p);
static void writel(u32 value, void *p);
static void memcpy_toio(void *dst, const void *src, size_t n);
static void memcpy_fromio(void *dst, const void *src, size_t n);
static void complete(struct completion *c) { c->done = true; }
static void reinit_completion(struct completion *c) { c->done = false; }
static unsigned wait_for_completion_timeout(struct completion *, unsigned);
static void disable_irq(int);
static void enable_irq(int irq) { assert(hw.irq_disabled); hw.irq_disabled=false; hw.enable++; }
static void sg_init_one(struct scatterlist *sg, const void *buf, unsigned n) { sg->len=n; }
static int dma_map_sg(void *dev, struct scatterlist *sg, int n, enum dma_data_direction d)
{
    assert(!hw.mapped);
    hw.map_direction=d;
    if (hw.map_fail) return 0;
    hw.maps++; hw.mapped=true; sg->address=hw.map_address;
    return 1;
}
#define sg_dma_address(sg) ((sg)->address)
static void dma_unmap_sg(void *dev, struct scatterlist *sg, int n, enum dma_data_direction d)
{ assert(hw.mapped); hw.unmap_direction=d; hw.unmaps++; hw.mapped=false; hw.unsafe |= hw.active; }
static struct dma_async_tx_descriptor *dmaengine_prep_slave_sg(void *c, struct scatterlist *sg,
        int n, enum dma_transfer_direction d, int flags)
{ hw.prep_direction=d; hw.prep++; return hw.prep_fail ? NULL : &descriptor; }
static int dmaengine_submit(struct dma_async_tx_descriptor *d)
{ hw.submit++; return hw.submit_fail ? -EIO : 1; }
static int dma_submit_error(int cookie) { return cookie < 0 ? cookie : 0; }
static int dmaengine_terminate_all(void *c)
{ hw.term++; if (hw.terminate_fail) return -EIO; hw.active=false; return 0; }
static void dma_wmb(void) { hw.barrier=++hw.serial; }
struct resource { u64 start; };
struct dma_slave_config {
    u64 src_addr, dst_addr;
    unsigned src_addr_width, dst_addr_width, src_maxburst, dst_maxburst;
};
static struct dma_slave_config configured;
static int dma_set_mask_and_coherent(void *dev,u64 mask)
{ hw.dma_mask=mask; return hw.mask_fail ? -EIO : 0; }
static void *dmam_alloc_coherent(void *dev,size_t n,dma_addr_t *addr,int flags)
{ assert(n==sizeof(desc)); *addr=0x12345000; return hw.alloc_fail ? NULL : &desc; }
static void *dma_request_chan(void *dev,const char *name)
{ assert(!strcmp(name,"rxtx")); return hw.channel_error ? (void *)(intptr_t)hw.channel_error : &descriptor; }
static int dmaengine_slave_config(void *chan, const struct dma_slave_config *conf)
{ configured=*conf; return hw.config_error; }
static long clk_round_rate(void *clk, unsigned long rate)
{ hw.clock_calls++; hw.requested_rate=rate; return hw.rounded_rate ? hw.rounded_rate : (long)rate; }
static int clk_set_rate(void *clk, unsigned long rate)
{ hw.clock_calls++; if (hw.clock_set_error) return hw.clock_set_error; hw.real_rate=rate; return 0; }
static int nand_soft_waitrdy(struct nand_chip *n, unsigned timeout)
{ hw.soft_wait_ms=timeout; return hw.fifo_busy ? -ETIMEDOUT : 0; }
#define readl_poll_timeout(addr, value, cond, delay, timeout) ({ \
    int poll_result=-ETIMEDOUT; hw.poll_calls++; \
    for (unsigned poll_i=0; poll_i<3; poll_i++) { \
        (value)=readl(addr); if (cond) { poll_result=0; break; } \
    } poll_result; })
#define sunxi_nand_lookup_timing(l,p,c) _sunxi_nand_lookup_timing(l,ARRAY_SIZE(l),p,c)
