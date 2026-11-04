/* SPDX-License-Identifier: GPL-2.0-or-later */
static void defaults(void)
{
    CHECK(!registered && !dma_live && !clock_live);
    for (unsigned i=0;i<nalloc;i++) CHECK(!allocations[i].live);
    memset(allocations,0,sizeof(allocations)); nalloc=events=fail_at=0;
    have_caps=slc=external_dma=true; with_calc=no_features=nvddr=false;
    options=0; engine=NAND_ECC_ENGINE_TYPE_ON_HOST; mode=4;
    writesize=2048; oobsize=128; req_step=1024; req_strength=16;
    host_mode=7; chip_mode[0]=chip_mode[1]=7; /* Unknown bootloader state. */
    timing_fault=extra_die_fault=0; reset_deasserted=false;
    mode_setup_calls=set_calls=get_calls=reset_calls=0;
    caps.has_ecc_clk=caps.has_mbus_clk=true;
}
static void release_devres(void)
{
    CHECK(!registered && !dma_live && !reset_deasserted);
    for (unsigned i=0;i<nalloc;i++)
        CHECK(!allocations[i].live || allocations[i].managed);
    for (unsigned i=nalloc;i;i--)
        if (allocations[i-1].live) release(allocations[i-1].p);
    clock_live=0;
}
static unsigned probe_case(unsigned failpoint, int variant)
{
    struct device_node nodes[3]={
        {.count=2,.cs={0,1},.rb={0,1}}, {.count=2,.cs={2,3},.rb={2,2}}, {0}};
    struct platform_device p={.dev.of_node=&nodes[2]};
    int ret;
    defaults(); fail_at=failpoint;
    nodes[2].child=nodes; nodes[0].next=&nodes[1];
    if (variant==1) with_calc=true;
    if (variant==2) engine=NAND_ECC_ENGINE_TYPE_SOFT;
    if (variant==3) engine=NAND_ECC_ENGINE_TYPE_NONE;
    if (variant==4) options=NAND_SKIP_BBTSCAN|NAND_ROM;
    if (variant==5) { nodes[0].vendor=nodes[1].vendor=true; no_features=true; }
    if (variant==6) { nodes[1].cs[0]=0; }
    if (variant==7) { req_step=req_strength=0; }
    if (variant==8) { nodes[1].count=0; }
    if (variant==9) { nodes[1].cs[0]=8; }
    if (variant==10) { have_caps=false; }
    if (variant==11) { extra_die_fault=1; }
    if (variant==12) { extra_die_fault=2; }
    if (variant==13) { extra_die_fault=3; }
    if (variant==14) { external_dma=false; slc=false; }
    if (variant==15) { req_strength=0; }
    if (variant==16) { nodes[1].count=-1; }
    if (variant>=17) {
        caps.has_ecc_clk=variant==18; caps.has_mbus_clk=variant==19;
    }
    ret=sunxi_nfc_probe(&p);
    if (!ret) {
        struct sunxi_nfc *n=p.data;
        CHECK(registered==2 && !list_empty(&n->chips));
        struct sunxi_nand_chip *c=list_first_entry(&n->chips,struct sunxi_nand_chip,node);
        CHECK(c->nand.base.memorg.ntargets==(extra_die_fault ? 1U : 2U));
        CHECK(c->nand.ecc.steps && c->nand.ecc.size);
        CHECK(c->nand.mtd.ecc_step_size==c->nand.ecc.size);
        CHECK(c->nand.pagecache.page==-1);
        CHECK(c->nand.current_interface_config==c->nand.best_interface_config);
        sunxi_nfc_remove(&p);
        CHECK(list_empty(&n->chips));
    } else {
        CHECK(ret<0 && !registered);
    }
    unsigned last=events;
    release_devres(); cases_run++; return last;
}
static void timing_cases(void)
{
    struct nand_controller ctrl={.ops=&sunxi_nand_controller_ops};
    struct nand_interface_config best={.timings.mode=4};
    for (int f=0;f<=6;f++) for (int features=0;features<3;features++) {
        defaults(); timing_fault=f;
        struct nand_chip c={.controller=&ctrl,.best_interface_config=&best,
            .parameters={.supports_set_get_features=true,
                .set_feature_list={features==0?0:BIT(1)},
                .get_feature_list={features==2?BIT(1):0}}};
        /* Boot handoff requires resetting the controller AND the NAND. */
        timing_fault=0;
        CHECK(nand_reset_interface(&c,0)==0 && host_mode==0);
        nand_select_target(&c,0); CHECK(!nand_reset_op(&c)); nand_deselect_target(&c);
        CHECK(chip_mode[0]==0);
        timing_fault=f;
        int ret=nand_setup_interface(&c,0);
        if ((f==1 && features) || f==2 || (f==3 && features==2)) CHECK(ret==-EIO);
        else CHECK(ret==0);
        if (features==2 && f>=4) {
            CHECK(c.current_interface_config==&reset_config);
            if (f==4) CHECK(!host_mode && !chip_mode[0]);
            if (f==5) CHECK(host_mode==4); /* Recovery callback failed, return ignored. */
            if (f==6) CHECK(chip_mode[0]==4); /* RESET transport failure, return ignored. */
        } else if (!ret) CHECK(c.current_interface_config==&best);
        CHECK(c.cur_cs==-1 || features==0);
        c.best_interface_config=NULL;
        unsigned before=mode_setup_calls; CHECK(!nand_setup_interface(&c,0)); CHECK(before==mode_setup_calls);
        cases_run++;
    }
    defaults();
    struct nand_controller_ops no_setup=sunxi_nand_controller_ops;
    no_setup.setup_interface=NULL; ctrl.ops=&no_setup;
    struct nand_chip c={.controller=&ctrl};
    CHECK(!nand_reset_interface(&c,0)); CHECK(!nand_setup_interface(&c,0));
    CHECK(nand_scan_with_ids(&c,0,NULL)==-EINVAL);
    cases_run++;
}
int main(void)
{
    /* Fault every event observed on each successful/invalid input path; later
     * cleanup allocations are checked by ownership, not just return status. */
    for (int variant=0;variant<20;variant++) {
        unsigned count=probe_case(0,variant);
        for (unsigned f=1;f<=count;f++) probe_case(f,variant);
    }
    timing_cases();
    printf("Lifecycle/ONFI cases: %u; assertions: %u\n",cases_run,assertions);
    puts("CONTRACT: timing mismatch plus failed recovery can return success; not a hardware observation");
    puts("ALL LIFECYCLE CHECKS PASSED");
    return 0;
}
