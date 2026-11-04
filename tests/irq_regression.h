/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef uint32_t u32;
typedef int irqreturn_t;
enum { IRQ_NONE, IRQ_HANDLED };
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define atomic_read(p) (*(p))
struct virt_dma_desc { unsigned completions, callbacks; };
struct sun6i_desc { struct virt_dma_desc vd; };
struct sun6i_vchan { struct { spinlock_t lock; } vc; bool cyclic; u32 irq_type; };
struct sun6i_pchan { struct sun6i_vchan *vchan; struct sun6i_desc *desc, *done; };
struct sun6i_dma_config { unsigned num_channels_per_reg, irq_stride, irq_stat_offset; };
struct sun6i_dma_dev {
    const struct sun6i_dma_config *cfg;
    unsigned num_pchans;
    struct sun6i_pchan *pchans;
    void *base;
    int tasklet_shutdown;
    unsigned task;
};
static u32 readl(void *p) { u32 v; memcpy(&v,p,4); return v; }
static void writel(u32 v, void *p) { v=readl(p)&~v; memcpy(p,&v,4); }
static void vchan_cookie_complete(struct virt_dma_desc *vd) { vd->completions++; }
static void vchan_cyclic_callback(struct virt_dma_desc *vd) { vd->callbacks++; }
static void tasklet_schedule(unsigned *task) { ++*task; }
