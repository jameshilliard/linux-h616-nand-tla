/* SPDX-License-Identifier: GPL-2.0-or-later */
static void free_desc(struct virt_dma_desc *vd)
{ struct sun6i_desc *d=container_of(vd,struct sun6i_desc,vd); assert(!d->freed++); }
static void callback(void *p)
{ struct sun6i_desc *d=p; assert(!d->freed && !d->callbacks++); }
static void setup(struct sun6i_dma_dev *dev, struct sun6i_pchan *phy,
                  struct sun6i_vchan *v, struct sun6i_desc *d, unsigned nr,
                  bool client_callback)
{
    memset(dev,0,sizeof(*dev)); memset(phy,0,16*sizeof(*phy));
    memset(v,0,16*sizeof(*v)); memset(d,0,16*sizeof(*d));
    memset(global_regs,0,sizeof(global_regs)); memset(channel_regs,0,sizeof(channel_regs));
    dev->base=global_regs; dev->pchans=phy; dev->num_pchans=nr;
    INIT_LIST_HEAD(&dev->slave.channels); INIT_LIST_HEAD(&dev->pending);
    tasklet_setup(&dev->task,sun6i_dma_tasklet);
    for (unsigned i=0;i<nr;i++) {
        phy[i].idx=i; phy[i].base=channel_regs[i];
        INIT_LIST_HEAD(&v[i].node);
        vchan_init(&v[i].vc,&dev->slave); v[i].vc.desc_free=free_desc;
        d[i].vd.tx=(struct dma_async_tx_descriptor){.chan=&v[i].vc.chan,.cookie=i+1,
            .callback=client_callback?callback:NULL,.callback_param=&d[i]}; d[i].p_lli=0x1000+i*64;
        INIT_LIST_HEAD(&d[i].vd.node); list_add_tail(&d[i].vd.node,&v[i].vc.desc_submitted);
        spin_lock(&v[i].vc.lock); assert(vchan_issue_pending(&v[i].vc)); spin_unlock(&v[i].vc.lock);
        list_add_tail(&v[i].node,&dev->pending);
    }
    sun6i_dma_tasklet(&dev->task);
    for (unsigned i=0;i<nr;i++) assert(phy[i].desc==&d[i] && v[i].phy==&phy[i]);
}
int main(void)
{
    sun4i_checks();
    struct sun6i_dma_dev dev; struct sun6i_pchan phy[16];
    struct sun6i_vchan v[16]; struct sun6i_desc d[16];
    unsigned cases=0, wrong_bank=0, missed_bank=0;
    for (unsigned client=0;client<2;client++)
    for (unsigned idx=0;idx<8;idx++)
    for (unsigned ordering=0;ordering<4;ordering++) {
        setup(&dev,phy,v,d,8,client);
        if (ordering==0) assert(!sun6i_dma_terminate_all(&v[idx].vc.chan));
        global_regs[4] = DMA_IRQ_QUEUE << (idx*DMA_IRQ_CHAN_WIDTH);
        assert(sun6i_dma_interrupt(0,&dev)==IRQ_HANDLED);
        if (ordering==1) assert(!sun6i_dma_terminate_all(&v[idx].vc.chan));
        if (ordering==2) tasklet_kill(&v[idx].vc.task);
        tasklet_kill(&dev.task);
        vchan_synchronize(&v[idx].vc);
        assert(d[idx].callbacks==(client && ordering>=2));
        assert(d[idx].freed==1);
        for (unsigned i=0;i<8;i++) {
            assert(!sun6i_dma_terminate_all(&v[i].vc.chan));
            vchan_synchronize(&v[i].vc); assert(d[i].freed==1);
        }
        cases++;
    }
    for (unsigned idx=8;idx<16;idx++) {
        setup(&dev,phy,v,d,16,false);
        global_regs[5] = DMA_IRQ_QUEUE << ((idx-8)*DMA_IRQ_CHAN_WIDTH);
        assert(sun6i_dma_interrupt(0,&dev)==IRQ_HANDLED);
        assert(d[idx].vd.tx.cookie && !d[idx-8].vd.tx.cookie);
        wrong_bank++;
        for (unsigned i=0;i<16;i++) { sun6i_dma_terminate_all(&v[i].vc.chan); vchan_synchronize(&v[i].vc); }
    }
    for (unsigned idx=8;idx<12;idx++) {
        setup(&dev,phy,v,d,12,false);
        global_regs[5] = DMA_IRQ_QUEUE << ((idx-8)*DMA_IRQ_CHAN_WIDTH);
        assert(sun6i_dma_interrupt(0,&dev)==IRQ_NONE);
        assert(global_regs[5] && d[idx].vd.tx.cookie); missed_bank++;
        for (unsigned i=0;i<12;i++) { sun6i_dma_terminate_all(&v[i].vc.chan); vchan_synchronize(&v[i].vc); }
    }
    /* Exercise cyclic cancellation and the descriptor-reuse list. */
    setup(&dev,phy,v,d,8,true);
    v[0].cyclic=true; vchan_cyclic_callback(&d[0].vd);
    sun6i_dma_terminate_all(&v[0].vc.chan); vchan_synchronize(&v[0].vc);
    assert(!d[0].callbacks && d[0].freed==1);
    d[1].vd.tx.reuse=true;
    global_regs[4]=DMA_IRQ_QUEUE<<DMA_IRQ_CHAN_WIDTH;
    sun6i_dma_interrupt(0,&dev); tasklet_kill(&dev.task); vchan_synchronize(&v[1].vc);
    assert(d[1].callbacks==1 && !d[1].freed);
    d[1].vd.tx.reuse=false;
    for (unsigned i=0;i<8;i++) { sun6i_dma_terminate_all(&v[i].vc.chan); vchan_synchronize(&v[i].vc); }
    printf("Provider completion: %u schedules; %u wrong-bank and %u unvisited-bank source witnesses\n",cases,wrong_bank,missed_bank);
    puts("ALL CHECKS PASSED");
}
