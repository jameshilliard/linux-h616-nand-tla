/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Execute the candidate helpers unchanged. AHB assertion retiring outstanding
 * transactions is the measured contract, not a consequence of dma_unmap_sg(). */
static void abort_recovery_checks(void)
{
    u8 input[2048];
    memset(input,0x5a,sizeof(input));
    unsigned trials=0;
    for (unsigned dir=0;dir<2;dir++)
    for (unsigned next_cs=0;next_cs<2;next_cs++)
    for (unsigned soft_fail=0;soft_fail<2;soft_fail++)
    /* Deassertion failure belongs only to the historical split-call fixture:
     * the actual CCU pulse callback unconditionally returns success. */
    for (unsigned deassert_fail=0;deassert_fail<(PULSE_RESET ? 1 : 2);deassert_fail++) {
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
        hw.bus_deassert_fail=deassert_fail;
        int ret=sunxi_nfc_dma_op_abort(&nfc);
        assert(ret==(deassert_fail ? -EIO : 0));
        assert(nfc.recovery_failed==deassert_fail && hw.reset_held==deassert_fail);
        assert(hw.irq_disabled==deassert_fail);
        assert(hw.bus_asserts==1 && hw.bus_deasserts==1);
        assert(hw.bus_pulses==(unsigned)PULSE_RESET);
        assert(hw.reset_hold_us==(PULSE_RESET ? 10U : 2U));
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
                assert(hw.bus_pulses==(PULSE_RESET ? repeat+2 : 0));
                assert(hw.reset_hold_us==(repeat+2)*(PULSE_RESET ? 10U : 2U));
                sunxi_nfc_dma_op_cleanup(&nfc,dir,&sg);
                assert(!sunxi_nfc_select_chip(&chip.nand,next_cs));
            }
        }
        assert(!hw.unsafe && hw.maps==hw.unmaps && !hw.frees);
        trials++;
    }
    unsigned boundaries=0;
    const int errors[]={-EIO,-ENODEV,-EPERM,PULSE_RESET ? -ENOTSUPP : -EOPNOTSUPP};
    for (unsigned dir=0;dir<2;dir++)
    for (unsigned active=0;active<2;active++)
    for (unsigned error=0;error<ARRAY_SIZE(errors);error++) {
        reset_fixture(2);
        caps.needs_bus_reset=true; nfc.reset=(void *)1;
        struct scatterlist sg;
        assert(!sunxi_nfc_dma_op_prepare(&nfc,input,1024,2,dir,&sg));
        hw.active=hw.outstanding_bus=active;
        hw.retain_stop_bus=hw.reset_fail=true;
        if (PULSE_RESET) hw.bus_reset_error=errors[error];
        else hw.bus_assert_error=errors[error];
        assert(sunxi_nfc_dma_op_abort(&nfc)==errors[error]);
        assert(hw.bus_pulses==(unsigned)PULSE_RESET);
        assert(hw.bus_asserts==(PULSE_RESET ? 0U : 1U) && !hw.bus_deasserts);
        assert(!hw.reset_hold_us);
        assert(nfc.recovery_failed && hw.irq_disabled && !hw.reset_held);
        unsigned accesses=hw.reads+hw.writes;
        sunxi_nfc_dma_op_cleanup(&nfc,dir,&sg);
        assert(hw.unmaps==1 && !hw.mapped && hw.unmap_direction==dir);
        assert(sunxi_nfc_select_chip(&chip.nand,0)==-EIO);
        assert(hw.reads+hw.writes==accesses);
        /* Explicit assumption witness, NOT a passing safety claim: a failed
         * assertion with DMA still active makes the current cleanup unsafe.
         * H616's validated CCU callback cannot produce this error; framework
         * rejection before reaching it is still an assumption boundary. */
        assert(hw.unsafe==(bool)active && hw.outstanding_bus==(bool)active);
        boundaries++;
    }
    printf("H616 streaming recovery: %u reset/direction/chip cases, repeated reuse, %s; %u error-return boundary cases retain the active-DMA counterexample\n",trials,PULSE_RESET ? "CCU pulse timing" : "held-reset unmap",boundaries);
}
