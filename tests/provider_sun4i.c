/* SPDX-License-Identifier: GPL-2.0-or-later */
static void s4_free(struct virt_dma_desc *vd)
{ struct sun4i_dma_contract *d=to_sun4i_dma_contract(vd); assert(!d->freed++); }
static unsigned s4_late_callbacks;
static void s4_callback(void *p)
{ struct sun4i_dma_contract *d=p; s4_late_callbacks+=!!d->freed; d->callbacks++; }
static void sun4i_checks(void)
{
    unsigned cases=0;
    for(unsigned client=0;client<2;client++)
    for(unsigned dedicated=0;dedicated<2;dedicated++)
    for(unsigned cyclic=0;cyclic<2;cyclic++)
    for(unsigned stop=0;stop<3;stop++) {
        struct sun4i_dma_config cfg={8,16};
        struct sun4i_dma_pchan phy[16]={0};
        struct sun4i_dma_vchan v[SUN4I_DMA_NR_MAX_VCHANS]={0};
        struct sun4i_dma_dev dev={.base=a10_regs,.cfg=&cfg,.pchans=phy,.vchans=v};
        struct sun4i_dma_contract d={.is_cyclic=cyclic,.use_half_int=cyclic};
        struct sun4i_dma_promise promise={.src=0x1000,.dst=0x2000,.len=1024,.cfg=BIT(31)};
        INIT_LIST_HEAD(&dev.slave.channels);
        memset(a10_regs,0,sizeof(a10_regs)); memset(a10_channel_regs,0,sizeof(a10_channel_regs));
        for(unsigned i=0;i<16;i++) { phy[i].base=a10_channel_regs[i]; phy[i].is_dedicated=i>=8; }
        for(unsigned i=0;i<ARRAY_SIZE(v);i++) { vchan_init(&v[i].vc,&dev.slave); v[i].vc.desc_free=s4_free; }
        v[0].is_dedicated=dedicated;
        d.vd.tx=(struct dma_async_tx_descriptor){.chan=&v[0].vc.chan,.cookie=1,.callback=client?s4_callback:NULL,.callback_param=&d};
        INIT_LIST_HEAD(&d.vd.node); INIT_LIST_HEAD(&d.demands); INIT_LIST_HEAD(&d.completed_demands);
        INIT_LIST_HEAD(&promise.list); list_add_tail(&promise.list,&d.demands);
        list_add_tail(&d.vd.node,&v[0].vc.desc_issued);
        spin_lock(&v[0].vc.lock); assert(!__execute_vchan_pending(&dev,&v[0])); spin_unlock(&v[0].vc.lock);
        unsigned physical=dedicated?8:0;
        assert(v[0].pchan==&phy[physical]);
        if(stop==0) sun4i_dma_terminate_all(&v[0].vc.chan);
        a10_regs[1]=BIT(2*physical+1);
        assert(sun4i_dma_interrupt(0,&dev)==IRQ_HANDLED);
        if(stop==1) sun4i_dma_terminate_all(&v[0].vc.chan);
        vchan_synchronize(&v[0].vc);
        assert(d.callbacks==(client && (stop==2 || (cyclic && stop==1))));
        if(stop==2) sun4i_dma_terminate_all(&v[0].vc.chan);
        vchan_synchronize(&v[0].vc);
        assert(d.freed==1 && !v[0].pchan && !v[0].processing && !dev.pchans_used[0]);
        cases++;
    }
    assert(s4_late_callbacks==2);
    printf("sun4i IRQ/dispatch/stop: %u normal/dedicated/cyclic schedules; %u freed-cyclic-callback witnesses (not a NAND transfer mode)\n",cases,s4_late_callbacks);
}
