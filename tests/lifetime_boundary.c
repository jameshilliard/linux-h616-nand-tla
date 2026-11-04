/* SPDX-License-Identifier: GPL-2.0-or-later */
int main(void)
{
    unsigned cases=0, witnesses=0;
    for (unsigned pm=0;pm<2;pm++)
    for (unsigned depth=1;depth<=3;depth++)
    for (unsigned users=0;users<=2;users++)
    for (unsigned close_first=0;close_first<2;close_first++) {
        struct sunxi_nfc nfc;
        struct sunxi_nand_chip sn={0};
        struct mtd_info part[3]={0}, *master=&sn.nand.mtd, *leaf=master;
        releases=cleanup_calls=owner_refs=module_refs=warnings=0;
        partitioned_master=pm; INIT_LIST_HEAD(&nfc.chips);
        INIT_LIST_HEAD(&sn.node); list_add_tail(&sn.node,&nfc.chips);
        sn.nand.resources=true;
        master->owner=&owner; master->name="fixture-master";
        INIT_LIST_HEAD(&master->partitions);
        if (pm) { master->dev.registered=true; master->refcnt.n=1; module_refs++; }
        mtd_idr[0]=master;
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
            witnesses++;
        }
        if (!close_first) for (unsigned u=0;u<users;u++) __put_mtd_device(leaf);
        assert(!owner_refs && !module_refs && releases==depth+pm);
        cases++;
    }
    struct mtd_info absent={.index=7};
    assert(del_mtd_device(&absent)==-ENODEV);
    module_available=false;
    assert(__get_mtd_device(&absent)==-ENODEV);
    printf("MTD lifetime: %u schedules; %u open-reference/resource-cleanup witnesses\n",cases,witnesses);
    puts("ALL CHECKS PASSED");
}
