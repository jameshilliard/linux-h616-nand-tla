/* SPDX-License-Identifier: GPL-2.0-or-later */
static void publish(struct mtd_info *m, struct mtd_info *master, unsigned index)
{
    m->index=index; m->registered=true; m->dev.registered=true;
    m->dev.type=&mtd_class; m->dev.data=m; m->dev.refs=1;
    if(EXTERNAL_DEVICE_OWNER) m->dev.release_owner=THIS_MODULE;
    kref_init(&m->refcnt); m->ref_master=master; mtd_get_lifetime(master);
    if(m->parent && m->parent!=master) m->parent->dev.refs++;
    assert(!mtd_ids[index]); mtd_ids[index]=true;
    mtd_idr[index]=m; core_module.refs++; m->np=master->np; m->np->refs++;
}

static struct mtd_info *new_partition(struct mtd_info *parent, unsigned index)
{
    struct mtd_info *m=calloc(1,sizeof(*m)); assert(m);
    m->parent=parent; m->name=strdup("partition"); m->type=1; m->flags=MTD_WRITEABLE;
    INIT_LIST_HEAD(&m->partitions); INIT_LIST_HEAD(&m->part.node);
    list_add_tail(&m->part.node,&parent->partitions);
    publish(m,mtd_get_master(parent),index); return m;
}

static struct nand_chip *waiting_chip;
static void disconnect_waiter(void) { nand_disconnect(waiting_chip); }
static unsigned notifier_reads;
static void removal_io(struct mtd_info *m)
{
    struct nand_chip *chip=mtd_to_nand(m);
    assert(!nand_get_device(chip)); nand_release_device(chip); notifier_reads++;
}

static void multi_chip_test(void)
{
    struct mtd_notifier notifier={.remove=removal_io};
    INIT_LIST_HEAD(&notifier.list); list_add_tail(&notifier.list,&mtd_notifiers);
    struct device parent={.refs=1};
    struct device_node node={0};
    struct sunxi_nfc *nfc=calloc(1,sizeof(*nfc)); assert(nfc);
    struct file files[2]={{.f_mode=FMODE_WRITE},{.f_mode=FMODE_WRITE}};
    struct inode inodes[2]={{.minor=0},{.minor=2}};
    nfc->dev=&parent; INIT_LIST_HEAD(&nfc->chips);
    assert(!devm_nand_controller_init(&parent,&nfc->controller,sunxi_nfc_release));
    cleaned=0; delay_release=false; driver_module.live=true;
    for(unsigned i=0;i<2;i++) {
        struct sunxi_nand_chip *sn=calloc(1,sizeof(*sn)); assert(sn);
        struct mtd_info *m=nand_to_mtd(&sn->nand);
        sn->nand.controller=&nfc->controller;
        INIT_LIST_HEAD(&sn->node); list_add_tail(&sn->node,&nfc->chips);
        m->np=&node; m->name="chip"; m->owner=&driver_module; m->dev.parent=&parent;
        m->type=1; m->flags=MTD_WRITEABLE; m->_sync=nand_sync;
        INIT_LIST_HEAD(&m->partitions);
        assert(!nand_set_release(&sn->nand,sunxi_nand_release)); publish(m,m,i);
        assert(!mtdchar_open(&inodes[i],&files[i]));
    }
    sunxi_nand_chips_cleanup(nfc); release_devres(&parent);
    assert(notifier_reads==2); list_del(&notifier.list);
    assert(!mtdchar_close(&inodes[0],&files[0]));
    assert(cleaned==1 && parent.refs==2);
    assert(!mtdchar_close(&inodes[1],&files[1]));
    assert(cleaned==2 && parent.refs==1 && !node.refs);
    assert(!driver_module.refs && !core_module.refs);
}

struct embedded_fixture { struct nand_controller controller; struct nand_chip chip; };
static unsigned controllers_freed;
static void embedded_release(struct nand_controller *controller)
{ controllers_freed++; free(container_of(controller,struct embedded_fixture,controller)); }

static void embedded_test(bool devres_first)
{
    struct device parent={.refs=1};
    struct embedded_fixture *f=calloc(1,sizeof(*f)); assert(f);
    struct mtd_info *m=&f->chip.mtd;
    unsigned before=controllers_freed;
    driver_module.live=true; delay_release=false;
    f->chip.controller=&f->controller; m->dev.parent=&parent; m->owner=&driver_module;
    assert(!devm_nand_controller_init(&parent,&f->controller,embedded_release));
    assert(!nand_set_release(&f->chip,NULL));
    assert(!strcmp(m->name,"controller") && parent.refs==2);
    nand_disconnect(&f->chip);
    if(devres_first) release_devres(&parent);
    assert(controllers_freed==before);
    mtd_put_master(m);
    if(!devres_first) { assert(controllers_freed==before); release_devres(&parent); }
    assert(controllers_freed==before+1 && parent.refs==1 && !driver_module.refs);
}

static void prepare_failure_test(void)
{
    struct device parent={.refs=1};
    struct embedded_fixture *f=calloc(1,sizeof(*f)); assert(f);
    unsigned before=controllers_freed;
    action_alloc_fail=true;
    assert(devm_nand_controller_init(&parent,&f->controller,embedded_release)==-ENOMEM);
    action_alloc_fail=false;
    assert(controllers_freed==before+1 && !parent.action);
    f=calloc(1,sizeof(*f)); assert(f);
    f->chip.controller=&f->controller; f->chip.mtd.dev.parent=&parent;
    assert(!devm_nand_controller_init(&parent,&f->controller,embedded_release));
    name_alloc_fail=true;
    assert(nand_set_release(&f->chip,NULL)==-ENOMEM);
    name_alloc_fail=false;
    assert(!f->chip.mtd.name && !f->chip.mtd.master.release);
    assert(f->controller.refcnt.n==1 && parent.refs==1);
    release_devres(&parent);
    assert(controllers_freed==before+2);
}

static void unmanaged_controller_test(void)
{
    struct nand_controller controller={0};
    struct sunxi_nand_chip *sn=calloc(1,sizeof(*sn)); assert(sn);
    struct mtd_info *m=&sn->nand.mtd;
    sn->nand.controller=&controller; m->name="unmanaged"; m->owner=&driver_module;
    assert(!nand_set_release(&sn->nand,sunxi_nand_release));
    nand_disconnect(&sn->nand); mtd_put_master(m);
    assert(!driver_module.refs);
}

/* Execute the actual MTD OTP pin/drop and unregister paths with provider shims. */
static void otp_test(bool master_registered, bool reverse)
{
    struct device parent={.refs=1};
    struct device_node node={0};
    struct nand_controller controller={0};
    struct sunxi_nand_chip *sn=calloc(1,sizeof(*sn)); assert(sn);
    struct mtd_info *m=&sn->nand.mtd;
    struct otp_provider providers[2]={{.mtd=m,.registered=true},
                                      {.mtd=m,.registered=true}};
    unsigned before=cleaned;
    sn->nand.controller=&controller; m->name="OTP"; m->owner=&driver_module;
    m->dev.parent=&parent; m->np=&node; INIT_LIST_HEAD(&m->partitions);
    assert(!nand_set_release(&sn->nand,sunxi_nand_release));
    if(EXTERNAL_OTP_OWNER) { __module_get(THIS_MODULE); __module_get(THIS_MODULE); }
    mtd_otp_nvmem_get(m); mtd_otp_nvmem_get(m);
    m->otp_user_nvmem=&providers[0]; m->otp_factory_nvmem=&providers[1];
    if(master_registered) publish(m,m,0);
    assert(!mtd_device_unregister(m));
#if HAS_OTP_CLEANUP_HELPER
    /* The historical helper also supports repeated cleanup. */
    mtd_otp_nvmem_remove(m);
    assert(!m->otp_user_nvmem && !m->otp_factory_nvmem);
#endif
    assert(!providers[0].registered && !providers[1].registered);
    nand_disconnect(&sn->nand); mtd_put_master(m);
    assert(cleaned==before && core_module.refs==2 && driver_module.refs==1);
    unsigned first=reverse?1:0, last=first^1;
    mtd_otp_nvmem_put(providers[first].mtd);
    if(EXTERNAL_OTP_OWNER) { assert(core_module.refs==2); module_put(THIS_MODULE); }
    assert(cleaned==before && core_module.refs==1);
    assert(nand_get_device(&sn->nand)==-ENODEV);
    mtd_otp_nvmem_put(providers[last].mtd);
    if(EXTERNAL_OTP_OWNER) { assert(core_module.refs==1); module_put(THIS_MODULE); }
    assert(cleaned==before+1 && !core_module.refs && !driver_module.refs);
    assert(parent.refs==1 && !node.refs);
}

int main(void)
{
    unsigned cases=0, opens=0;
    for(unsigned depth=0;depth<=2;depth++)
    for(unsigned master_registered=0;master_registered<2;master_registered++)
    for(unsigned delayed=0;delayed<2;delayed++)
    for(unsigned writable=0;writable<2;writable++)
    for(unsigned close_first=0;close_first<2;close_first++)
    for(unsigned extra_device_ref=0;extra_device_ref<2;extra_device_ref++)
    for(unsigned unload=0;unload<3;unload++) {
        if(!depth && !master_registered) continue;
        if(unload && !close_first) continue; /* Open handles pin the provider. */
        if(unload && extra_device_ref) continue;
        assert(!driver_module.refs && !core_module.refs && !npending);
        struct device parent={.refs=1}; /* fixture ownership */
        struct device_node node={0};
        struct sunxi_nfc *nfc=calloc(1,sizeof(*nfc)); assert(nfc);
        struct sunxi_nand_chip *sn=calloc(1,sizeof(*sn)); assert(sn);
        struct nand_chip *chip=&sn->nand;
        struct mtd_info *master=nand_to_mtd(chip), *leaf=master;
        nfc->dev=&parent; INIT_LIST_HEAD(&nfc->chips);
        assert(!devm_nand_controller_init(&parent,&nfc->controller,sunxi_nfc_release));
        INIT_LIST_HEAD(&sn->node); list_add_tail(&sn->node,&nfc->chips);
        chip->controller=&nfc->controller; master->np=&node;
        master->name="NAND master"; master->owner=&driver_module; master->dev.parent=&parent;
        master->type=1; master->flags=MTD_WRITEABLE; master->_sync=nand_sync;
        INIT_LIST_HEAD(&master->partitions);
        assert(!nand_set_release(chip,sunxi_nand_release));
        cleaned=part_freed=0; delay_release=delayed; driver_module.live=true;
        if(master_registered) publish(master,master,0);
        for(unsigned d=1;d<=depth;d++) leaf=new_partition(leaf,d);
        struct device *held_dev=&leaf->dev;
        if(extra_device_ref) held_dev->refs++;
        struct inode inode={.minor=leaf->index*2};
        struct file files[3]={{.f_mode=writable?FMODE_WRITE:0},
                              {.f_mode=writable?FMODE_WRITE:0},{0}};
        assert(get_mtd_device(leaf,-1)==leaf); put_mtd_device(leaf);
        driver_module.live=false;
        assert(mtdchar_open(&inode,&files[2])==-ENODEV);
        driver_module.live=true;
        fd_alloc_fail=true; assert(mtdchar_open(&inode,&files[2])==-ENOMEM); fd_alloc_fail=false;
        for(unsigned u=0;u<2;u++) { assert(!mtdchar_open(&inode,&files[u])); opens++; }
        if(close_first) for(unsigned u=0;u<2;u++) assert(!mtdchar_close(&inode,&files[u]));
        /* A suspended reader wakes when disconnect occurs, without resume. */
        assert(!nand_suspend(master)); waiting_chip=chip; suspend_wait_hook=disconnect_waiter;
        assert(nand_get_device(chip)==-ENODEV); suspend_wait_hook=NULL;
        assert(nand_suspend(master)==(DISCONNECTED_PM_OK ? 0 : -ENODEV)); nand_resume(master);
        assert(nand_mtd_get_device(master)==-ENODEV);
        if(unload) {
            assert(!driver_module.refs);
            driver_module.live=unload==2;
            driver_module.state=unload==1 ? MODULE_STATE_GOING : MODULE_STATE_COMING;
        }
        sunxi_nand_chips_cleanup(nfc); assert(list_empty(&nfc->chips));
        release_devres(&parent); /* last devres action, after hardware teardown */
        assert(mtdchar_open(&inode,&files[2])==-ENODEV);
        if(!close_first) {
            assert(!cleaned && parent.refs==2);
            assert(nand_get_device(chip)==-ENODEV);
            for(unsigned u=0;u<2;u++) assert(!mtdchar_close(&inode,&files[u]));
        }
        if(extra_device_ref) {
            assert(!cleaned && driver_module.refs && parent.refs==2);
            assert(mtd_get_master(leaf)==master);
            put_device(held_dev);
        }
        drain_devices();
        assert(cleaned==1 && part_freed==depth && !node.refs && parent.refs==1);
        assert(!driver_module.refs && !core_module.refs);
        driver_module.live=true; driver_module.state=MODULE_STATE_LIVE;
        for(unsigned i=0;i<16;i++) assert(!mtd_idr[i] && !mtd_ids[i]);
        cases++;
    }
    /* Completion callback used when device registration failed. */
    struct completion done={0}; struct device failed={.data=&done};
    mtd_dev_release_done(&failed); assert(done.done);
    multi_chip_test(); cases++; opens+=2;
    embedded_test(false); embedded_test(true); prepare_failure_test(); unmanaged_controller_test();
    cases+=4;
    for(unsigned registered=0;registered<2;registered++)
        for(unsigned reverse=0;reverse<2;reverse++) {
            otp_test(registered,reverse); cases++;
        }
    printf("Retained lifetime: %u schedules, %u successful file opens/closes\n",cases,opens);
    puts("ALL CHECKS PASSED");
}
