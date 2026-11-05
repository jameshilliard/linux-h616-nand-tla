/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Execute the candidate helpers unchanged. AHB assertion retiring outstanding
 * transactions is the measured contract, not a consequence of dma_unmap_sg(). */
static jmp_buf stalled_request;
static bool observe_permanent_stall;

static void inspect_retry(void)
{
    assert(hw.mapped && hw.maps==1 && !hw.unmaps && !hw.frees);
    assert(hw.irq_disabled && hw.outstanding_bus && !hw.reset_held);
    assert(!nfc.recovery_failed);
    /* Escape only the host fixture after witnessing four retries. The kernel
     * request must NOT return when a reset provider permanently fails. */
    if (observe_permanent_stall && hw.retry_sleeps==4)
        longjmp(stalled_request,1);
}

static void abort_recovery_checks(void)
{
    u8 input[2048];
    memset(input,0x5a,sizeof(input));
    unsigned trials=0;
    recovery_retry=inspect_retry;
    for (unsigned dir=0;dir<2;dir++)
    for (unsigned next_cs=0;next_cs<2;next_cs++)
    for (unsigned soft_fail=0;soft_fail<2;soft_fail++)
    for (unsigned assert_failures=0;assert_failures<4;assert_failures++)
    for (unsigned deassert_fail=0;deassert_fail<2;deassert_fail++) {
        reset_fixture(2);
        caps.needs_bus_reset=source_caps[2].needs_bus_reset;
        assert(caps.needs_bus_reset);
        nfc.mdma_desc=NULL; nfc.use_mdma=false; nfc.reset=(void *)1;
        struct resource resource={.start=0x1c03000};
        assert(!sunxi_nfc_dma_init(&nfc,&resource));
        assert(nfc.mdma_desc==&desc && !nfc.mdma_buf && !hw.allocs);
        struct scatterlist sg;
        assert(!sunxi_nfc_dma_op_prepare(&nfc,input,1024,2,dir,&sg));
        assert(hw.maps==1 && hw.mapped && le32toh(desc.buf)==hw.map_address);
        assert(hw.map_direction==dir);
        hw.active=hw.outstanding_bus=true;
        hw.retain_stop_bus=true;
        hw.reset_fail=soft_fail;
        hw.bus_assert_failures=assert_failures;
        hw.bus_deassert_fail=deassert_fail;
        int ret=sunxi_nfc_dma_op_abort(&nfc);
        assert(ret==(deassert_fail ? -EIO : 0));
        assert(nfc.recovery_failed==deassert_fail && hw.reset_held==deassert_fail);
        assert(hw.irq_disabled==deassert_fail);
        assert(hw.bus_asserts==assert_failures+1 && hw.retry_sleeps==assert_failures);
        assert(hw.mapped && !hw.active && !hw.outstanding_bus);
        unsigned accesses=hw.reads+hw.writes;
        sunxi_nfc_dma_op_cleanup(&nfc,dir,&sg);
        assert(hw.unmaps==1 && !hw.mapped && hw.unmap_direction==dir);
        if (deassert_fail) assert(hw.reads+hw.writes==accesses);
        accesses=hw.reads+hw.writes;
        ret=sunxi_nfc_select_chip(&chip.nand,next_cs);
        if (deassert_fail) assert(ret==-EIO && hw.reads+hw.writes==accesses);
        else {
            assert(!ret && load(NFC_REG_TIMING_CFG)==chip.timing_cfg);
            assert(((load(NFC_REG_CTL)>>24)&7)==chip.sels[next_cs].cs);
            assert(load(caps.reg_spare_area)==2048);
            for(unsigned repeat=0;repeat<3;repeat++) {
                assert(!sunxi_nfc_dma_op_prepare(&nfc,input,1024,2,dir,&sg));
                hw.active=hw.outstanding_bus=true;
                assert(!sunxi_nfc_dma_op_abort(&nfc));
                sunxi_nfc_dma_op_cleanup(&nfc,dir,&sg);
                assert(!sunxi_nfc_select_chip(&chip.nand,next_cs));
            }
        }
        assert(!hw.unsafe && hw.maps==hw.unmaps && !hw.frees);
        trials++;
    }
    reset_fixture(2);
    caps.needs_bus_reset=true; nfc.reset=(void *)1;
    struct scatterlist sg;
    assert(!sunxi_nfc_dma_op_prepare(&nfc,input,1024,2,DMA_FROM_DEVICE,&sg));
    hw.active=hw.outstanding_bus=hw.retain_stop_bus=hw.bus_assert_fail=true;
    observe_permanent_stall=true;
    if (!setjmp(stalled_request)) {
        sunxi_nfc_dma_op_abort(&nfc);
        assert(!"permanently failing reset must not return caller-owned memory");
    }
    assert(hw.retry_sleeps==4 && hw.mapped && !hw.unmaps && !hw.unsafe);
    observe_permanent_stall=false; recovery_retry=NULL;
    printf("H616 streaming recovery: %u reset/direction/chip cases, repeated reuse, held-reset unmap, permanent-stall ownership\n",trials);
}
