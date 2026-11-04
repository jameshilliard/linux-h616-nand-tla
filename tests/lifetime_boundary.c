/* SPDX-License-Identifier: GPL-2.0-or-later */
static unsigned io_after_cleanup;
static int retained_read(struct mtd_info *m,loff_t from,struct mtd_oob_ops *ops)
{
    struct nand_chip *n=container_of(m,struct nand_chip,mtd);
    assert(from==17 && ops->len==1);
    if(!n->resources) { io_after_cleanup++; return -EIO; }
    ops->retlen=1; *ops->datbuf=0x96; return 0;
}
int main(void)
{
    unsigned cases=0, witnesses=0, reopened=0;
    for (unsigned pm=0;pm<2;pm++)
    for (unsigned depth=1;depth<=3;depth++)
    for (unsigned users=0;users<=2;users++)
    for (unsigned close_first=0;close_first<2;close_first++) {
        struct sunxi_nfc nfc;
        struct sunxi_nand_chip sn={0};
        struct mtd_info part[3]={0}, *master=&sn.nand.mtd, *leaf=master;
        releases=cleanup_calls=owner_refs=module_refs=warnings=0;
        memset(mtd_idr,0,sizeof(mtd_idr));
        partitioned_master=pm; INIT_LIST_HEAD(&nfc.chips);
        INIT_LIST_HEAD(&sn.node); list_add_tail(&sn.node,&nfc.chips);
        sn.nand.resources=true;
        sn.registered=true;
        master->owner=&owner; master->name="fixture-master"; master->_read_oob=retained_read;
        INIT_LIST_HEAD(&master->partitions);
        if (pm) { master->dev.registered=true; master->refcnt.n=1; module_refs++; }
        if(pm) mtd_idr[0]=master;
        for (unsigned d=0;d<depth;d++) {
            struct mtd_info *p=&part[d];
            p->parent=leaf; p->index=d+1; p->name="fixture-partition";
            p->dev.registered=true; p->refcnt.n=1; module_refs++;
            INIT_LIST_HEAD(&p->partitions); INIT_LIST_HEAD(&p->part.node);
            list_add_tail(&p->part.node,&leaf->partitions);
            mtd_idr[d+1]=p; leaf=p;
        }
        for (unsigned u=0;u<users;u++) assert(!__get_mtd_device(leaf));
        assert(owner_refs==users && leaf->refcnt.n==users+1);
        if (close_first) for (unsigned u=0;u<users;u++) __put_mtd_device(leaf);
        sunxi_nand_chips_cleanup(&nfc);
        assert(cleanup_calls==1 && !warnings && !sn.nand.resources && list_empty(&nfc.chips));
        if (!close_first && users) {
            /* Exact source drops registration refs but does not drain users. */
            assert(leaf->dev.registered && leaf->refcnt.n==users && owner_refs==users);
            u8 byte=0; struct mtd_oob_ops op={.len=1,.datbuf=&byte};
            assert(mtd_read_oob_std(leaf,17,&op)==-EIO && !op.retlen);
            /* A live ref can also retain lookup publication after unregister. */
            assert(get_mtd_device(NULL,leaf->index)==leaf);
            assert(owner_refs==users+1);
            __put_mtd_device(leaf); reopened++;
            witnesses++;
        }
        if (!close_first) for (unsigned u=0;u<users;u++) __put_mtd_device(leaf);
        assert(!owner_refs && !module_refs && releases==depth+pm);
        assert(get_mtd_device(NULL,leaf->index)==ERR_PTR(-ENODEV));
        cases++;
    }
    unsigned multi=0;
    for(unsigned held=0;held<4;held++) {
        struct sunxi_nfc nfc; struct sunxi_nand_chip sn[2]={0};
        INIT_LIST_HEAD(&nfc.chips); memset(mtd_idr,0,sizeof(mtd_idr));
        releases=cleanup_calls=owner_refs=module_refs=warnings=0; partitioned_master=true;
        for(unsigned i=0;i<2;i++) {
            struct mtd_info *m=&sn[i].nand.mtd;
            INIT_LIST_HEAD(&sn[i].node); list_add_tail(&sn[i].node,&nfc.chips);
            INIT_LIST_HEAD(&m->partitions); sn[i].nand.resources=true;
            sn[i].registered=true;
            m->owner=&owner; m->index=i; m->name="fixture-multi-chip";
            m->_read_oob=retained_read; m->dev.registered=true; m->refcnt.n=1;
            mtd_idr[i]=m; module_refs++;
            if(held&BIT(i)) assert(!__get_mtd_device(m));
        }
        sunxi_nand_chips_cleanup(&nfc);
        assert(cleanup_calls==2 && list_empty(&nfc.chips));
        for(unsigned i=0;i<2;i++) {
            struct mtd_info *m=&sn[i].nand.mtd;
            if(held&BIT(i)) {
                u8 byte; struct mtd_oob_ops op={.len=1,.datbuf=&byte};
                assert(mtd_read_oob_std(m,17,&op)==-EIO);
                __put_mtd_device(m);
            }
            assert(get_mtd_device(NULL,i)==ERR_PTR(-ENODEV));
        }
        assert(releases==2 && !owner_refs && !module_refs && !warnings); multi++;
    }
    struct mtd_info absent={.index=7};
    assert(del_mtd_device(&absent)==-ENODEV);
    module_available=false;
    assert(__get_mtd_device(&absent)==-ENODEV);
    printf("MTD lifetime: %u partition schedules, %u two-chip schedules; %u partition cleanup witnesses; %u later I/O calls; %u later opens\n",
           cases,multi,witnesses,io_after_cleanup,reopened);
    puts("ALL CHECKS PASSED");
}
