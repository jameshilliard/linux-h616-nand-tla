/* SPDX-License-Identifier: GPL-2.0-or-later */
static void defaults(void)
{
    CHECK(!registered && !dma_live && !clock_live);
    CHECK(!held_user);
    hold_first_user=false;
    detects=unavailable_detects=scan_with_busy_controller=0;
    scanned=register_calls=unregister_calls=0; expected_scans=2;
    for (unsigned i=0;i<nalloc;i++) CHECK(!allocations[i].live);
    memset(allocations,0,sizeof(allocations)); nalloc=events=fail_at=0;
    have_caps=slc=external_dma=true; with_calc=no_features=nvddr=false;
    options=0; engine=NAND_ECC_ENGINE_TYPE_ON_HOST; mode=4;
    writesize=2048; oobsize=128; req_step=1024; req_strength=16;
    host_mode=7; chip_mode[0]=chip_mode[1]=7; /* Unknown bootloader state. */
    timing_fault=extra_die_fault=0; reset_deasserted=false;
    reset_pulses=reset_deasserts=0;
    mode_setup_calls=set_calls=get_calls=reset_calls=0;
    caps.has_ecc_clk=caps.has_mbus_clk=true;
    caps.needs_bus_reset=false;
}
static void release_devres(void);
static void visibility_cases(void)
{
    const char *statuses[]={NULL,"okay","ok","disabled","reserved","fail","fail-1"};
    unsigned witnesses=0, runs=0;
    for (unsigned st=0;st<ARRAY_SIZE(statuses);st++)
        for (unsigned first=0;first<2;first++) for (unsigned present=0;present<2;present++) {
            struct device_node nodes[3]={
                {.count=1,.cs={0},.rb={0}}, {.count=1,.cs={1},.rb={1}}, {0}};
            struct platform_device p={.dev.of_node=&nodes[2]};
            defaults();
            nodes[2].child=nodes; nodes[0].next=&nodes[1];
            unsigned index=first?0:1;
            nodes[index].status=statuses[st]; nodes[index].absent=!present;
            bool skipped=FILTERED_CHILDREN && !of_device_is_available(&nodes[index]);
            expected_scans=skipped?1:2;
            int ret=sunxi_nfc_probe(&p);
            if (skipped) CHECK(!ret && registered==1 && detects==1);
            else if (present) CHECK(!ret && registered==2 && detects==2);
            else CHECK(ret==-ENODEV && !registered && detects==index+1);
            CHECK(unavailable_detects==(!skipped && st>=3));
            witnesses+=unavailable_detects;
            if (!ret) sunxi_nfc_remove(&p);
            release_devres(); runs++; cases_run++;
        }
    printf("DT availability: %u cases, %u unavailable-child scan witnesses\n",runs,witnesses);

    /* Both children are valid and probe succeeds. No unbind or rollback is
     * needed to expose this independent scan-versus-live-I/O conflict. */
    struct device_node nodes[3]={
        {.count=1,.cs={0},.rb={0}}, {.count=1,.cs={1},.rb={1}}, {0}};
    struct platform_device p={.dev.of_node=&nodes[2]};
    defaults(); nodes[2].child=nodes; nodes[0].next=&nodes[1]; hold_first_user=true;
    CHECK(!sunxi_nfc_probe(&p));
    CHECK(registered==2 && detects==2);
    CHECK(scan_with_busy_controller==(DEFERRED_PUBLICATION?0:1));
    CHECK(held_user && held_user->lock && held_user->controller->lock);
    nand_release_device(held_user); held_user=NULL;
    sunxi_nfc_remove(&p); release_devres(); cases_run++;
    if (DEFERRED_PUBLICATION)
        puts("FIX VERIFIED: no identification after MTD publication while first user holds controller->lock");
    else
        puts("SOURCE WITNESS: second nand_scan_ident reaches detection while first MTD user holds controller->lock");
}
static unsigned publication_case(unsigned children, unsigned fault)
{
    struct device_node nodes[4]={
        {.count=1,.cs={0},.rb={0}}, {.count=1,.cs={1},.rb={1}},
        {.count=1,.cs={2},.rb={2}}, {0}};
    struct platform_device p={.dev.of_node=&nodes[3]};
    defaults(); expected_scans=children; fail_at=fault;
    if (children) nodes[3].child=nodes;
    for (unsigned i=1;i<children;i++) nodes[i-1].next=&nodes[i];
    int ret=sunxi_nfc_probe(&p);
    if (!ret) {
        CHECK(scanned==children && registered==children && register_calls==children);
        sunxi_nfc_remove(&p);
        CHECK(unregister_calls==children);
    } else {
        CHECK(ret<0 && !registered);
        if (DEFERRED_PUBLICATION && scanned<children)
            CHECK(!register_calls && !unregister_calls);
        if (register_calls) CHECK(unregister_calls<=register_calls);
    }
    unsigned last=events;
    release_devres(); cases_run++;
    return last;
}
static void publication_cases(void)
{
    unsigned before=cases_run;
    for (unsigned n=0;n<=3;n++) {
        unsigned count=publication_case(n,0);
        for (unsigned f=1;f<=count;f++) publication_case(n,f);
    }
    /* All unavailable children must leave no scan, CS claim or MTD behind. */
    struct device_node nodes[3]={
        {.count=1,.cs={0},.status="disabled"},
        {.count=1,.cs={1},.status="reserved"}, {0}};
    struct platform_device p={.dev.of_node=&nodes[2]};
    defaults(); nodes[2].child=nodes; nodes[0].next=&nodes[1];
    expected_scans=FILTERED_CHILDREN?0:2;
    CHECK(!sunxi_nfc_probe(&p));
    CHECK(registered==expected_scans && detects==expected_scans);
    struct sunxi_nfc *nfc=p.data;
    CHECK(nfc->assigned_cs==(FILTERED_CHILDREN?0UL:3UL));
    sunxi_nfc_remove(&p); release_devres(); cases_run++;
    printf("Probe publication/unwind: %u cases (0-3 children, all event failures, all-unavailable control)\n",
           cases_run-before);
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
    if (variant>=17 && variant<=19) {
        caps.has_ecc_clk=variant==18; caps.has_mbus_clk=variant==19;
    }
    if (variant==20) caps.needs_bus_reset=true;
    ret=sunxi_nfc_probe(&p);
    if (!ret) {
        struct sunxi_nfc *n=p.data;
        CHECK(reset_pulses==(unsigned)(PULSE_RESET && caps.needs_bus_reset));
        CHECK(reset_deasserts==(PULSE_RESET && caps.needs_bus_reset ? 0U : 1U));
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
        else if (FIXED_TIMING && features==2 && f>=5) CHECK(ret==-EIO);
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
    for (int variant=0;variant<20+MANDATORY_RESET;variant++) {
        unsigned count=probe_case(0,variant);
        for (unsigned f=1;f<=count;f++) probe_case(f,variant);
    }
    timing_cases();
    visibility_cases();
    publication_cases();
    printf("Lifecycle/ONFI cases: %u; assertions: %u\n",cases_run,assertions);
    if (FIXED_TIMING)
        puts("FIX VERIFIED: timing mismatch recovery propagates controller and RESET failures");
    else
        puts("CONTRACT: timing mismatch plus failed recovery can return success; not a hardware observation");
    puts("ALL LIFECYCLE CHECKS PASSED");
    return 0;
}
