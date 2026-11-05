/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exact candidate helpers; reset retirement is a stated hardware contract.
 * Failure injection does not claim CCU callbacks fail on ordinary H616s. */
static void abort_recovery_checks(void)
{
    u8 input[2048];
    for (unsigned i=0;i<sizeof(input);i++) input[i]=(i*19+(i>>8)*7)&255;
    unsigned trials=0;
    for (unsigned dir=0;dir<2;dir++)
    for (unsigned next_cs=0;next_cs<2;next_cs++)
    for (unsigned fault=0;fault<6;fault++) {
        reset_fixture(2);
        caps.needs_bus_reset=source_caps[2].needs_bus_reset;
        assert(caps.needs_bus_reset);
        nfc.mdma_desc=NULL; nfc.use_mdma=false; nfc.reset=(void *)1;
        struct resource resource={.start=0x1c03000};
        assert(!sunxi_nfc_dma_init(&nfc,&resource));
        assert(nfc.mdma_buf==coherent_buffer && nfc.mdma_buf_size==32768);
        struct scatterlist sg;
        assert(!sunxi_nfc_dma_op_prepare(&nfc,input,1024,2,dir,&sg));
        assert(!hw.maps && le32toh(desc.buf)==nfc.mdma_buf_dma);
        if(dir==DMA_TO_DEVICE) assert(!memcmp(input,nfc.mdma_buf,sizeof(input)));
        hw.active=hw.outstanding_bus=true;
        hw.reset_fail=fault==4 || fault==5;
        if(fault==1) nfc.reset=NULL;
        hw.bus_assert_fail=fault==2 || fault==4;
        hw.bus_deassert_fail=fault==3;
        int ret=sunxi_nfc_dma_op_abort(&nfc);
        bool failed=fault>=1 && fault<=4;
        assert((ret<0)==failed && nfc.recovery_failed==failed);
        assert(nfc.dma_unsafe==(fault==1 || fault==2 || fault==4));
        assert(hw.irq_disabled==failed);
        unsigned writes=hw.writes;
        ret=sunxi_nfc_select_chip(&chip.nand,next_cs);
        if(failed) assert(ret==-EIO && hw.writes==writes);
        else {
            assert(!ret && !hw.active && !hw.outstanding_bus);
            assert(load(NFC_REG_TIMING_CFG)==chip.timing_cfg);
            assert(((load(NFC_REG_CTL)>>24)&7)==chip.sels[next_cs].cs);
            assert(load(caps.reg_spare_area)==2048);
            /* Reuse has to work repeatedly, not just on the first reset. */
            for(unsigned repeat=0;repeat<3;repeat++) {
                hw.active=hw.outstanding_bus=true;
                assert(!sunxi_nfc_dma_op_abort(&nfc));
                assert(!sunxi_nfc_select_chip(&chip.nand,next_cs));
            }
        }
        sunxi_nfc_dma_release(&nfc);
        assert(hw.frees==(nfc.dma_unsafe ? 0 : 2));
        assert(!hw.unsafe && !hw.unmaps);
        trials++;
    }
    reset_fixture(2); nfc.mdma_desc=NULL; nfc.use_mdma=false;
    hw.buffer_alloc_fail=true;
    struct resource resource={0};
    assert(!sunxi_nfc_dma_init(&nfc,&resource));
    assert(!nfc.mdma_desc && !nfc.mdma_buf && !nfc.use_mdma && hw.frees==1);
    printf("H616 recovery: %u direction/chip/reset-failure cases, repeated reuse, retained memory and allocation unwind\n",trials);
}
