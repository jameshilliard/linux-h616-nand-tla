/* SPDX-License-Identifier: GPL-2.0-or-later */
struct sun4i_dma_vchan;
struct sun4i_dma_pchan { void *base; bool is_dedicated; struct sun4i_dma_vchan *vchan; };
struct sun4i_dma_promise { struct list_head list; u32 src,dst,len,para,cfg; };
struct sun4i_dma_contract {
    struct virt_dma_desc vd;
    struct list_head demands,completed_demands;
    bool is_cyclic,use_half_int;
    unsigned callbacks,freed;
};
struct sun4i_dma_vchan {
    struct virt_dma_chan vc;
    struct sun4i_dma_pchan *pchan;
    struct sun4i_dma_promise *processing;
    struct sun4i_dma_contract *contract;
    bool is_dedicated;
};
struct sun4i_dma_config { unsigned ndma_nr_max_channels,dma_nr_max_channels; };
struct sun4i_dma_dev {
    struct dma_device slave; struct lock lock; void *base;
    struct sun4i_dma_pchan *pchans;
    struct sun4i_dma_vchan *vchans;
    struct sun4i_dma_config *cfg;
    unsigned long pchans_used[1];
};
#define to_sun4i_dma_dev(d) container_of(d,struct sun4i_dma_dev,slave)
#define to_sun4i_dma_vchan(c) container_of(c,struct sun4i_dma_vchan,vc.chan)
#define to_sun4i_dma_contract(d) container_of(d,struct sun4i_dma_contract,vd)
#define for_each_set_bit(i,p,n) for((i)=0;(i)<(n);(i)++) if((*(p))&(1UL<<(i)))
#define for_each_clear_bit_from(i,p,n) for(;(i)<(n);(i)++) if(!((*(p))&(1UL<<(i))))
static void set_bit(unsigned i,unsigned long *p) { *p|=1UL<<i; }
static void clear_bit(unsigned i,unsigned long *p) { *p&=~(1UL<<i); }
static u32 a10_regs[2],a10_channel_regs[16][8];
#define readl_relaxed(p) readl(p)
static void writel_relaxed(u32 v,void *p)
{ if(p==&a10_regs[1]) v=readl(p)&~v; memcpy(p,&v,4); }
