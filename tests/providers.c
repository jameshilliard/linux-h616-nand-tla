/* SPDX-License-Identifier: GPL-2.0-or-later */
int main(void)
{
    unsigned cases=0;
    for (unsigned present=0;present<2;present++)
    for (unsigned dedicated=0;dedicated<2;dedicated++)
    for (unsigned queued=0;queued<4;queued++) {
        u32 registers[2]={1,1};
        struct sun4i_dma_dev dev={0};
        struct sun4i_dma_pchan phy={.base=registers,.is_dedicated=dedicated};
        struct sun4i_dma_vchan v={.pchan=present?&phy:NULL,.processing=&phy,.vc={.queued=queued}};
        struct dma_chan chan={.device=&dev,.private=&v};
        struct dma_slave_config cfg={.src_addr=0x1000,.dst_addr=0x1000,.src_addr_width=4,
            .dst_addr_width=4,.src_maxburst=4,.dst_maxburst=4};
        mmio_writes=irq_masks=released=freed=terminated=0;
        assert(!sun4i_dma_config(&chan,&cfg) && !memcmp(&v.cfg,&cfg,sizeof(cfg)));
        assert(!sun4i_dma_terminate_all(&chan));
        assert(!v.pchan && !v.processing && !v.vc.lock.held && !v.vc.queued);
        assert(mmio_writes==present && irq_masks==present && released==present && freed==queued);
        assert(registers[0]==!present && registers[1]==1);
        assert(!sun4i_dma_terminate_all(&chan) && freed==queued && mmio_writes==present);
        cases++;
    }
    for (unsigned present=0;present<2;present++)
    for (unsigned active=0;active<3;active++)
    for (unsigned cyclic=0;cyclic<2;cyclic++)
    for (unsigned queued=0;queued<4;queued++) {
        u32 registers[2]={1,2}; struct sun6i_dma_dev dev={0};
        struct sun6i_desc descriptor={0};
        struct sun6i_pchan phy={.base=registers,.desc=active?&descriptor:NULL,
                               .done=active==2?&descriptor:NULL};
        struct sun6i_vchan v={.phy=present?&phy:NULL,.node={1},.cyclic=cyclic,.vc={.queued=queued}};
        phy.vchan=&v;
        struct dma_chan chan={.device=&dev,.private=&v};
        struct dma_slave_config cfg={.src_addr=0x2000,.dst_addr=0x2000,.src_addr_width=4,
            .dst_addr_width=4,.src_maxburst=8,.dst_maxburst=8};
        mmio_writes=irq_masks=released=freed=terminated=0;
        assert(!sun6i_dma_config(&chan,&cfg) && !memcmp(&v.cfg,&cfg,sizeof(cfg)));
        assert(!sun6i_dma_terminate_all(&chan));
        assert(!v.phy && !v.cyclic && !v.node.count && !v.vc.queued);
        assert(!v.vc.lock.held && !dev.lock.held && freed==queued);
        assert(mmio_writes==present*2 && terminated==(present && active==1));
        if (present) assert(!registers[0] && !registers[1] && !phy.vchan && !phy.desc && !phy.done);
        assert(!sun6i_dma_terminate_all(&chan) && freed==queued && mmio_writes==present*2);
        cases++;
    }
    printf("Provider config/stop: %u sun4i/sun6i exact-entrypoint cases\n",cases);
    puts("ALL PROVIDER CHECKS PASSED");
}
