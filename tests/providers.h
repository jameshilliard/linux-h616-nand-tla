/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exact provider entrypoints with list/virt-dma/lock/MMIO boundary fixtures.
 * This does NOT model real provider IRQ/tasklet concurrency or bus retirement. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32;
#define BIT(n) (1U << (n))
struct lock { bool held; };
struct list_head { unsigned count; };
#define LIST_HEAD(n) struct list_head n={0}
static void spin_lock(struct lock *l) { assert(!l->held); l->held=true; }
static void spin_unlock(struct lock *l) { assert(l->held); l->held=false; }
#define spin_lock_irqsave(l,f) do { (f)=0; spin_lock(l); } while (0)
#define spin_unlock_irqrestore(l,f) do { (void)(f); spin_unlock(l); } while (0)
struct dma_chan { void *device, *private; };
struct dma_slave_config { unsigned src_addr,dst_addr,src_addr_width,dst_addr_width,src_maxburst,dst_maxburst; };
struct virt_dma_chan { struct lock lock; unsigned queued; };
struct virt_dma_desc { bool terminated; };
struct sun4i_dma_dev { int unused; };
struct sun4i_dma_pchan { bool is_dedicated; void *base; };
struct sun4i_dma_vchan { struct virt_dma_chan vc; struct sun4i_dma_pchan *pchan; void *processing; struct dma_slave_config cfg; };
struct sun6i_desc { struct virt_dma_desc vd; };
struct sun6i_vchan;
struct sun6i_pchan { void *base; struct sun6i_desc *desc,*done; struct sun6i_vchan *vchan; };
struct sun6i_dma_dev { struct lock lock; };
struct sun6i_vchan { struct virt_dma_chan vc; struct sun6i_pchan *phy; struct list_head node; bool cyclic; struct dma_slave_config cfg; };
#define to_sun4i_dma_dev(x) ((struct sun4i_dma_dev *)(x))
#define to_sun4i_dma_vchan(x) ((struct sun4i_dma_vchan *)((x)->private))
#define to_sun6i_dma_dev(x) ((struct sun6i_dma_dev *)(x))
#define to_sun6i_vchan(x) ((struct sun6i_vchan *)((x)->private))
static unsigned mmio_writes,irq_masks,released,freed,terminated;
static void writel(u32 value,void *p) { memcpy(p,&value,4); mmio_writes++; }
static void list_del_init(struct list_head *h) { h->count=0; }
static void vchan_get_all_descriptors(struct virt_dma_chan *v,struct list_head *h)
{ assert(v->lock.held); h->count=v->queued; v->queued=0; }
static void vchan_dma_desc_free_list(struct virt_dma_chan *v,struct list_head *h)
{ freed+=h->count; h->count=0; }
static void vchan_terminate_vdesc(struct virt_dma_desc *d)
{ assert(!d->terminated); d->terminated=true; terminated++; }
static void set_pchan_interrupt(struct sun4i_dma_dev *d,struct sun4i_dma_pchan *p,int a,int b)
{ assert(!a && !b); irq_masks++; }
static void release_pchan(struct sun4i_dma_dev *d,struct sun4i_dma_pchan *p) { released++; }
