/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef uint32_t u32;
typedef int dma_cookie_t;
typedef int irqreturn_t;
enum { IRQ_NONE, IRQ_HANDLED };
struct tasklet_struct { bool pending; void (*fn)(struct tasklet_struct *); };
#define from_tasklet(p,t,m) container_of(t,__typeof__(*(p)),m)
static void tasklet_setup(struct tasklet_struct *t, void (*f)(struct tasklet_struct *)) { t->fn=f; }
static void tasklet_schedule(struct tasklet_struct *t) { t->pending=true; }
static void tasklet_kill(struct tasklet_struct *t) { if (t->pending) { t->pending=false; t->fn(t); } }
struct dma_device { struct list_head channels; void *dev; };
struct dma_chan { struct dma_device *device; struct list_head device_node; };
struct dmaengine_result { int result; unsigned residue; };
struct dma_async_tx_descriptor {
    struct dma_chan *chan;
    int cookie;
    bool reuse;
    void (*callback)(void *);
    void *callback_param;
};
struct dmaengine_desc_callback { void (*callback)(void *); void *param; };
static void dma_cookie_init(struct dma_chan *c) { }
static void dma_cookie_complete(struct dma_async_tx_descriptor *d) { assert(d->cookie); d->cookie=0; }
static bool dmaengine_desc_test_reuse(struct dma_async_tx_descriptor *d) { return d->reuse; }
static void dmaengine_desc_get_callback(struct dma_async_tx_descriptor *d, struct dmaengine_desc_callback *c)
{ c->callback=d->callback; c->param=d->callback_param; }
static void dmaengine_desc_callback_invoke(struct dmaengine_desc_callback *c, const struct dmaengine_result *r)
{ if (c->callback) c->callback(c->param); }
/* @virt-structs@ */
struct sun6i_desc { struct virt_dma_desc vd; void *v_lli; u32 p_lli; unsigned freed, callbacks; };
struct sun6i_vchan;
struct sun6i_pchan { unsigned idx; void *base; struct sun6i_desc *desc,*done; struct sun6i_vchan *vchan; };
struct sun6i_vchan { struct virt_dma_chan vc; struct sun6i_pchan *phy; struct list_head node; bool cyclic; u32 irq_type; };
struct sun6i_dma_dev {
    struct dma_device slave;
    struct lock lock;
    struct tasklet_struct task;
    struct list_head pending;
    unsigned num_pchans;
    struct sun6i_pchan *pchans;
    void *base;
    int tasklet_shutdown;
};
#define to_sun6i_dma_dev(d) container_of(d,struct sun6i_dma_dev,slave)
#define to_sun6i_vchan(c) container_of(c,struct sun6i_vchan,vc.chan)
#define to_sun6i_desc(t) container_of(t,struct sun6i_desc,vd.tx)
#define atomic_read(p) (*(p))
#define sun6i_dma_dump_lli(...) ((void)0)
#define sun6i_dma_dump_com_regs(...) ((void)0)
#define sun6i_dma_dump_chan_regs(...) ((void)0)
static u32 global_regs[16], channel_regs[16][8];
static u32 readl(const void *p) { u32 v; memcpy(&v,p,4); return v; }
static void writel(u32 v, void *p)
{
    /* IRQ status has write-one-to-clear semantics. */
    if ((uintptr_t)p >= (uintptr_t)&global_regs[4] && (uintptr_t)p < (uintptr_t)&global_regs[8])
        v=readl(p)&~v;
    memcpy(p,&v,4);
}
