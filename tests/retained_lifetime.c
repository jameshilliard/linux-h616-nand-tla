/* SPDX-License-Identifier: GPL-2.0-or-later */
static void publish(struct mtd_info *m, struct mtd_info *master, unsigned index)
{
    m->index=index; m->registered=true; m->dev.registered=true;
    m->dev.type=&mtd_class; m->dev.data=m; m->dev.refs=1;
    kref_init(&m->refcnt); m->ref_master=master; mtd_get_lifetime(master);
    if(m->parent && m->parent!=master) m->parent->dev.refs++;
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

static void multi_chip_test(void)
{
    struct device parent={.refs=2};
    struct device_node node={0};
    struct sunxi_nfc *nfc=calloc(1,sizeof(*nfc)); assert(nfc);
    struct file files[2]={{.f_mode=FMODE_WRITE},{.f_mode=FMODE_WRITE}};
    struct inode inodes[2]={{.minor=0},{.minor=2}};
    nfc->dev=&parent; kref_init(&nfc->refcnt); INIT_LIST_HEAD(&nfc->chips);
    cleaned=0; delay_release=false; driver_module.live=true;
    for(unsigned i=0;i<2;i++) {
        struct sunxi_nand_chip *sn=calloc(1,sizeof(*sn)); assert(sn);
        struct mtd_info *m=nand_to_mtd(&sn->nand);
        sn->nand.controller=&nfc->controller;
        INIT_LIST_HEAD(&sn->node); list_add_tail(&sn->node,&nfc->chips);
        m->np=&node; node.refs++; m->name=strdup("chip"); m->owner=&driver_module;
        m->type=1; m->flags=MTD_WRITEABLE; m->_sync=nand_sync;
        m->_get_device=sunxi_nand_get_device; INIT_LIST_HEAD(&m->partitions);
        kref_get(&nfc->refcnt); mtd_set_release(m,sunxi_nand_release); publish(m,m,i);
        assert(!mtdchar_open(&inodes[i],&files[i]));
    }
    sunxi_nand_chips_cleanup(nfc); sunxi_nfc_put(nfc);
    assert(!mtdchar_close(&inodes[0],&files[0]));
    assert(cleaned==1 && parent.refs==2);
    assert(!mtdchar_close(&inodes[1],&files[1]));
    assert(cleaned==2 && parent.refs==1 && !node.refs);
    assert(!driver_module.refs && !core_module.refs);
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
    for(unsigned unload=0;unload<2;unload++) {
        if(!depth && !master_registered) continue;
        if(unload && !close_first) continue; /* Open handles pin the provider. */
        if(unload && extra_device_ref) continue;
        assert(!driver_module.refs && !core_module.refs && !npending);
        struct device parent={.refs=2}; /* fixture and controller ownership */
        struct device_node node={.refs=1}; /* retained flash node */
        struct sunxi_nfc *nfc=calloc(1,sizeof(*nfc)); assert(nfc);
        struct sunxi_nand_chip *sn=calloc(1,sizeof(*sn)); assert(sn);
        struct nand_chip *chip=&sn->nand;
        struct mtd_info *master=nand_to_mtd(chip), *leaf=master;
        kref_init(&nfc->refcnt); nfc->dev=&parent; INIT_LIST_HEAD(&nfc->chips);
        INIT_LIST_HEAD(&sn->node); list_add_tail(&sn->node,&nfc->chips);
        chip->controller=&nfc->controller; master->np=&node;
        master->name=strdup("NAND master"); master->owner=&driver_module;
        master->type=1; master->flags=MTD_WRITEABLE; master->_sync=nand_sync;
        master->_get_device=sunxi_nand_get_device; INIT_LIST_HEAD(&master->partitions);
        kref_get(&nfc->refcnt); mtd_set_release(master,sunxi_nand_release);
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
        assert(nand_suspend(master)==-ENODEV); nand_resume(master);
        assert(sunxi_nand_get_device(master)==-ENODEV);
        if(unload) { assert(!driver_module.refs); driver_module.live=false; }
        sunxi_nand_chips_cleanup(nfc); assert(list_empty(&nfc->chips));
        sunxi_nfc_put(nfc); /* last devres action, after hardware teardown */
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
        for(unsigned i=0;i<16;i++) assert(!mtd_idr[i]);
        cases++;
    }
    /* Completion callback used when device registration failed. */
    struct completion done={0}; struct device failed={.data=&done};
    mtd_dev_release_done(&failed); assert(done.done);
    multi_chip_test(); cases++; opens+=2;
    printf("Retained lifetime: %u schedules, %u successful file opens/closes\n",cases,opens);
    puts("ALL CHECKS PASSED");
}
