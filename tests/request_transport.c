/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual prepare/wait/abort/cleanup bodies run in this translation unit.
 * Event timing and DMA retirement are explicit hardware contracts, not proof
 * of real bus behavior. The complete page callers choose the call sequence. */
static struct scatterlist request_sg;
static unsigned request_backend, request_fault, request_nth, request_launches;
static unsigned request_prepares;
static bool request_pending, request_timeout;

static void request_observe(const char *kind, int arg)
{
    if (!strcmp(kind, "map") || !strcmp(kind, "unmap") ||
        !strcmp(kind, "terminate-ok") || !strcmp(kind, "terminate-error"))
        request_event(kind, arg, 0);
}

void request_snapshot(unsigned *mapped, unsigned *active,
                      unsigned *unsafe, unsigned *mask)
{
    *mapped=hw.mapped; *active=hw.active || hw.outstanding_bus;
    *unsafe=hw.unsafe; *mask=readl(nfc.regs+NFC_REG_INT);
}

void request_config(unsigned backend, unsigned fault, unsigned nth)
{
    request_backend=backend; request_fault=fault; request_nth=nth;
    request_launches=request_prepares=0;
    request_pending=request_timeout=false;
    nfc.use_mdma=backend!=0; nfc.mdma_desc=backend==2 ? &desc : NULL;
    nfc.mdma_desc_dma=0x1000; nfc.dmac=&descriptor;
    caps.has_mdma=backend==1; caps.has_mdma_desc=backend==2;
    hw.backend=backend; hw.map_address=0x2000;
    recovery_observe=request_observe;
}

void request_reuse(unsigned cs)
{
    /* Different rate with the same chip, or same rate with another chip. */
    chip.clk_rate=cs ? 24000000 : 48000000;
    request_event("reuse", cs, chip.clk_rate);
}

int request_prepare(const void *buf, unsigned size, unsigned count, int dir)
{
    request_prepares++;
    hw.map_fail=request_fault==1 && request_prepares==request_nth;
    int ret=sunxi_nfc_dma_op_prepare(&nfc,buf,size,count,dir,&request_sg);
    hw.map_fail=false;
    request_event("prepare",ret,dir);
    return ret;
}

void request_launch(bool dma)
{
    request_launches++;
    request_timeout=request_fault==4 ? request_launches<=2 :
        request_fault>=2 && request_launches==request_nth;
    request_pending=true;
    if (dma) hw.active=hw.outstanding_bus=true;
    /* H616 clears status at launch. Older profiles rely on acknowledged
     * status from the preceding wait, not undocumented automatic clearing. */
    if (request_backend==2) {
        u32 zero=readl(nfc.regs+NFC_REG_ST)&~7U;
        memcpy(nfc.regs+NFC_REG_ST,&zero,4);
    }
    request_event("launch",dma,request_timeout);
}

static void request_complete(unsigned events)
{
    hw.active=hw.outstanding_bus=false;
    u32 st=readl(nfc.regs+NFC_REG_ST)|events;
    memcpy(nfc.regs+NFC_REG_ST,&st,4);
}

int request_wait(unsigned events, bool poll, unsigned timeout)
{
    assert(request_pending);
    if (!request_timeout) request_complete(events);
    int ret=sunxi_nfc_wait_events(&nfc,events,poll,timeout);
    request_pending=false;
    request_event("wait",ret,events);
    return ret;
}

int request_fifo(void) { return sunxi_nfc_wait_cmd_fifo_empty(&nfc); }

void request_abort(void)
{
    hw.terminate_fail=hw.reset_fail=request_fault==3;
    sunxi_nfc_dma_op_abort(&nfc);
    hw.terminate_fail=hw.reset_fail=false;
    request_event("abort",request_fault==3 ? -EIO : 0,0);
}

void request_cleanup(int dir)
{
    sunxi_nfc_dma_op_cleanup(&nfc,dir,&request_sg);
    request_event("cleanup",dir,0);
}

/* This MMIO fixture implements the strong reset contract used by positive
 * cases. Failed reset deliberately keeps ownership unsafe. */
static void request_mmio_write(u32 value, void *ptr)
{
    unsigned off=offset(ptr);
    if (off==NFC_REG_CTL && (value&NFC_RESET) && !hw.reset_fail) {
        hw.active=hw.outstanding_bus=false;
        value=0;
    }
    media_write_reg(off,value);
}
