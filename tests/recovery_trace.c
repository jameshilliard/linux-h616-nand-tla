/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Driver helpers are real; DMA execution/event timing below is a contract.
 * This script does not replace the separate page-caller or provider tests. */
static FILE *rt_output;
static bool rt_first;
static unsigned rt_epoch, rt_seen, rt_schedule, rt_contract;

static unsigned rt_required(void)
{ return NFC_CMD_INT_FLAG | (hw.backend ? NFC_DMA_INT_FLAG : 0); }

static void rt_observe(const char *kind, int arg)
{
    fprintf(rt_output, "%s{\"kind\":\"%s\",\"arg\":%d,"
        "\"mapped\":%s,\"active\":%s,\"bus\":%s,\"mask\":%u,\"status\":%u,"
        "\"complete\":%s,\"disabled\":%s,\"maps\":%u,\"unmaps\":%u,"
        "\"ram\":%s,\"unsafe\":%s,\"context\":[%u,%u,%u,%u,%u,%lu]}",
        rt_first ? "" : ",", kind, arg,
        hw.mapped ? "true" : "false", hw.active ? "true" : "false",
        hw.outstanding_bus ? "true" : "false", load(NFC_REG_INT), load(NFC_REG_ST)&7,
        nfc.complete.done ? "true" : "false", hw.irq_disabled ? "true" : "false",
        hw.maps, hw.unmaps, (load(NFC_REG_CTL)&NFC_RAM_METHOD) ? "true" : "false",
        hw.unsafe ? "true" : "false", (load(NFC_REG_CTL)>>24)&7,
        load(NFC_REG_TIMING_CTL), load(NFC_REG_TIMING_CFG), load(caps.reg_spare_area),
        !!(load(NFC_REG_CTL)&NFC_EN), nfc.clk_rate);
    rt_first=false;
}

static void rt_arrive(unsigned events, bool old)
{
    if(!old) {
        rt_seen|=events;
        if((rt_seen&rt_required())==rt_required()) {
            hw.active=false;
            /* Counterfactual completion without retirement, not a claim
             * about what any particular SoC/provider actually does. */
            hw.outstanding_bus=rt_contract==4;
        }
    }
    store(NFC_REG_ST,load(NFC_REG_ST)|events);
    rt_observe(old ? "arrive-old" : "arrive",events);
}

static void rt_irq(void)
{ sunxi_nfc_interrupt(1,&nfc); }

static unsigned rt_wait(struct completion *c)
{
    switch(rt_schedule) {
    case 0: /* Finished before wait_events() arms the interrupt. */
        rt_irq(); break;
    case 1:
        rt_arrive(NFC_CMD_INT_FLAG,false); rt_irq();
        if(hw.backend) { rt_arrive(NFC_DMA_INT_FLAG,false); rt_irq(); }
        break;
    case 2:
        if(hw.backend) { rt_arrive(NFC_DMA_INT_FLAG,false); rt_irq(); }
        rt_arrive(NFC_CMD_INT_FLAG,false); rt_irq();
        break;
    case 3: /* Device status wins the timeout recheck, without a handler. */
        rt_arrive(rt_required(),false); return 0;
    case 4: /* Only CMD acknowledged; integrated DMA is still pending. */
    case 5: /* The remaining handler runs while disable_irq() drains it. */
        if(hw.backend) { rt_arrive(NFC_CMD_INT_FLAG,false); rt_irq(); }
        return 0;
    case 6: /* No event arrives. */
        return 0;
    case 7: /* An old command's events arrive after the next launch. */
        rt_arrive(rt_required(),true); rt_irq(); break;
    default:
        assert(false);
    }
    return c->done;
}

static void rt_drain(void)
{
    if(rt_schedule!=5) return;
    rt_arrive(hw.backend ? NFC_DMA_INT_FLAG : NFC_CMD_INT_FLAG,false);
    rt_irq();
}

static void rt_cycle(unsigned cs, unsigned direction)
{
    struct scatterlist sg;
    /* The pinned v9 selector returns void; v10 propagates clock errors.
     * These successful-clock cases check the actual resulting context. */
    sunxi_nfc_select_chip(&chip.nand,cs);
    rt_observe("select",chip.sels[cs].cs);
    int ret=sunxi_nfc_dma_op_prepare(&nfc,media,1024,1,direction,&sg);
    rt_observe("prepare",ret);
    assert(!ret);

    /* Caller/hardware boundary: the low-level harness does not execute the
     * complete ECC page operation. H616 clears CMD/DMA at FSM start; do not
     * assign that undocumented automatic clearing to older profiles. */
    if(hw.backend==2) store(NFC_REG_ST,load(NFC_REG_ST)&~rt_required());
    rt_epoch++; rt_seen=0;
    hw.active=hw.outstanding_bus=true;
    rt_observe("launch",rt_epoch);
    if(!rt_schedule) rt_arrive(rt_required(),false);
    rt_observe("wait-start",rt_required());
    ret=sunxi_nfc_wait_events(&nfc,rt_required(),false,1);
    rt_observe("wait-return",ret);
    assert(ret==((rt_schedule==4 || rt_schedule==6) ? -ETIMEDOUT : 0));
    if(ret) {
        sunxi_nfc_dma_op_abort(&nfc);
        rt_observe("abort-return",0);
    }
    sunxi_nfc_dma_op_cleanup(&nfc,direction,&sg);
    rt_observe("cleanup-return",0);
}

static void rt_case(unsigned backend, unsigned direction, unsigned schedule,
                    unsigned clear, unsigned next_cs, unsigned contract)
{
    reset_fixture(backend);
    rt_first=true; rt_epoch=0; rt_seen=0;
    rt_schedule=schedule; rt_contract=contract;
    hw.reset_clears_context=clear;
    hw.reset_fail=hw.terminate_fail=contract==1;
    hw.retain_stop_bus=contract==2;
    fprintf(rt_output,"{\"backend\":%u,\"direction\":%u,\"schedule\":%u,"
        "\"clear\":%u,\"nextCs\":%u,\"contract\":%u,\"events\":[",
        backend,direction,schedule,clear,next_cs,contract);
    recovery_observe=rt_observe; recovery_wait=rt_wait; recovery_drain=rt_drain;
    rt_cycle(0,direction);
    if(contract==0 || contract==3) {
        rt_schedule=contract==3 ? 7 : 1;
        rt_cycle(next_cs,direction);
    }
    rt_observe("finish",rt_epoch);
    fputs("]}\n",rt_output);
    assert(!hw.mapped && hw.maps==hw.unmaps && hw.unsafe==(contract!=0));
    recovery_observe=NULL; recovery_wait=NULL; recovery_drain=NULL;
}

static void recovery_traces(void)
{
    rt_output=fopen("recovery-traces.jsonl","w");
    assert(rt_output);
    unsigned count=0;
    for(unsigned backend=0;backend<3;backend++)
    for(unsigned direction=0;direction<2;direction++)
    for(unsigned clear=0;clear<2;clear++)
    for(unsigned next_cs=0;next_cs<2;next_cs++) {
        for(unsigned schedule=0;schedule<7;schedule++) {
            rt_case(backend,direction,schedule,clear,next_cs,0); count++;
        }
        for(unsigned contract=1;contract<=4;contract++) {
            rt_case(backend,direction,contract==4 ? 0 : 6,clear,next_cs,contract);
            count++;
        }
    }
    assert(!fclose(rt_output));
    printf("Recovery source traces: %u schedules (168 normal; 96 conditional contract witnesses)\n",count);
}
