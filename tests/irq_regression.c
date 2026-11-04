/* SPDX-License-Identifier: GPL-2.0-or-later */
int main(void)
{
    unsigned cases=0, failures=0;
    const unsigned counts[]={8,12,16,24,32};
    for (unsigned layout=0;layout<2;layout++)
    for (unsigned n=0;n<ARRAY_SIZE(counts);n++)
    for (unsigned cyclic=0;cyclic<2;cyclic++)
    for (unsigned active=0;active<counts[n];active++) {
        struct sun6i_dma_config cfg={
            .num_channels_per_reg=layout ? DMA_IRQ_CHAN_NR_A733 : DMA_IRQ_CHAN_NR_A31,
            .irq_stride=layout ? DMA_IRQ_STRIDE_A733 : DMA_IRQ_STRIDE_A31,
            .irq_stat_offset=layout ? DMA_IRQ_STAT_OFFSET_A733 : DMA_IRQ_STAT_OFFSET_A31,
        };
        struct sun6i_pchan *p=calloc(counts[n],sizeof(*p));
        struct sun6i_vchan *v=calloc(counts[n],sizeof(*v));
        struct sun6i_desc *d=calloc(counts[n],sizeof(*d));
        u32 regs[1024]={0};
        assert(p && v && d);
        struct sun6i_dma_dev dev={.cfg=&cfg,.num_pchans=counts[n],.pchans=p,.base=regs};
        for (unsigned i=0;i<counts[n];i++) {
            p[i].vchan=&v[i]; p[i].desc=&d[i];
            v[i].cyclic=cyclic; v[i].irq_type=cyclic ? DMA_IRQ_PKG : DMA_IRQ_QUEUE;
        }
        unsigned bank=active/cfg.num_channels_per_reg;
        unsigned reg=(cfg.irq_stat_offset+bank*cfg.irq_stride)/4;
        regs[reg]=v[active].irq_type << ((active%cfg.num_channels_per_reg)*DMA_IRQ_CHAN_WIDTH);
        /* Unused fields must be acknowledged but never index past pchans. */
        if (counts[n]%cfg.num_channels_per_reg && bank==counts[n]/cfg.num_channels_per_reg)
            regs[reg]|=DMA_IRQ_QUEUE<<((cfg.num_channels_per_reg-1)*DMA_IRQ_CHAN_WIDTH);
        int ret=sun6i_dma_interrupt(0,&dev);
        bool correct=ret==IRQ_HANDLED && !regs[reg];
        for (unsigned i=0;i<counts[n];i++) {
            correct &= d[i].vd.completions==(!cyclic && i==active);
            correct &= d[i].vd.callbacks==(cyclic && i==active);
            if (!EXPECT_BUG && !cyclic && i==active) assert(p[i].done==&d[i]);
        }
        if (!correct) failures++;
        if (!EXPECT_BUG) assert(correct);
        free(d); free(v); free(p); cases++;
    }
    assert(EXPECT_BUG ? failures>0 : failures==0);
    printf("sun6i IRQ dispatch: %u cases, %u misrouted/missed events%s\n",cases,failures,
           EXPECT_BUG ? " (baseline reproduced)" : " (candidate fixed)");
    puts("ALL CHECKS PASSED");
}
