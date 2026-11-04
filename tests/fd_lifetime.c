/* SPDX-License-Identifier: GPL-2.0-or-later */
static void fd_emit(FILE *out,const char *kind,struct mtd_info *leaf,
                    struct nand_chip *chip,int ret)
{
    fprintf(out,"[\"%s\",%d,%u,%u,%u,%u,%u,%u]\n",kind,ret,leaf->refcnt.n,
            owner_refs,leaf->dev.registered,chip->resources,dead_lock_accesses,
            mtd_idr[leaf->index]!=NULL);
}

int main(void)
{
    FILE *out=fopen("fd-traces.jsonl","w"); assert(out);
    unsigned scenarios=0,sync_witnesses=0,reopens=0;
    for(unsigned partition=0;partition<2;partition++)
    for(unsigned writable=0;writable<2;writable++)
    for(unsigned close_first=0;close_first<2;close_first++)
    for(unsigned users=1;users<=2;users++) {
        struct sunxi_nfc nfc={0};
        struct sunxi_nand_chip sn={0}; struct mtd_info part={0};
        struct mtd_info *master=&sn.nand.mtd, *leaf=partition ? &part : master;
        struct file files[3]={0}; struct inode inode={.minor=partition ? 2 : 0};
        memset(mtd_idr,0,sizeof(mtd_idr));
        releases=cleanup_calls=owner_refs=module_refs=warnings=dead_lock_accesses=0;
        dead_chip_lock=dead_controller_lock=NULL;
        partitioned_master=true; module_available=true; fd_alloc_fail=false;
        INIT_LIST_HEAD(&nfc.chips); INIT_LIST_HEAD(&sn.node);
        list_add_tail(&sn.node,&nfc.chips); sn.registered=true;
        sn.nand.resources=true; sn.nand.controller=&nfc;
        master->owner=&owner; master->name="fixture-master"; master->_sync=nand_sync;
        master->type=1; master->flags=MTD_WRITEABLE;
        INIT_LIST_HEAD(&master->partitions);
        master->dev.registered=true; master->refcnt.n=1; module_refs=1;
        mtd_idr[0]=master;
        if(partition) {
            part.parent=master; part.index=1; part.name="fixture-partition";
            part.type=1; part.flags=MTD_WRITEABLE;
            part.refcnt.n=1; part.dev.registered=true; module_refs++;
            INIT_LIST_HEAD(&part.partitions); INIT_LIST_HEAD(&part.part.node);
            list_add_tail(&part.part.node,&master->partitions); mtd_idr[1]=&part;
        }
        fprintf(out,"{\"case\":%u,\"partition\":%u,\"write\":%u,\"closeFirst\":%u,\"users\":%u}\n",
                scenarios,partition,writable,close_first,users);
        fd_emit(out,"init",leaf,&sn.nand,0);
        for(unsigned u=0;u<users;u++) {
            files[u].f_mode=writable ? FMODE_WRITE : 0;
            assert(!mtdchar_open(&inode,&files[u])); fd_opens++;
            fd_emit(out,"open",leaf,&sn.nand,0);
        }
        if(close_first) for(unsigned u=0;u<users;u++) {
            assert(!mtdchar_close(&inode,&files[u])); fd_closes++;
            fd_emit(out,"close",leaf,&sn.nand,0);
        }
        sunxi_nand_chips_cleanup(&nfc);
        /* The driver cleanup and subsequent devres release have distinct
         * owners. Flag their former locks after remove has returned. */
        dead_chip_lock=&sn.nand.lock; dead_controller_lock=&nfc.lock;
        assert(!sn.nand.resources && list_empty(&nfc.chips));
        fd_emit(out,"cleanup",leaf,&sn.nand,0);
        int ret=mtdchar_open(&inode,&files[2]);
        assert(ret==(close_first ? -ENODEV : 0));
        fd_emit(out,"reopen",leaf,&sn.nand,ret);
        if(!ret) {
            reopens++; fd_opens++;
            assert(!mtdchar_close(&inode,&files[2])); fd_closes++;
            fd_emit(out,"close",leaf,&sn.nand,0);
        }
        if(!close_first) for(unsigned u=0;u<users;u++) {
            assert(!mtdchar_close(&inode,&files[u])); fd_closes++;
            fd_emit(out,"close",leaf,&sn.nand,0);
        }
        assert(dead_lock_accesses==(!close_first && writable ? users*2 : 0));
        if(dead_lock_accesses) sync_witnesses++;
        assert(!owner_refs && !module_refs && !warnings && !leaf->refcnt.n);
        assert(!sn.nand.lock.held && !nfc.lock.held);
        fd_emit(out,"finish",leaf,&sn.nand,0); scenarios++;
    }
    assert(!fclose(out));
    printf("MTD fd lifetime: %u schedules, %u opens/closes, %u retained-publication opens, %u close/sync witnesses\n",
           scenarios,fd_opens,reopens,sync_witnesses);
    assert(fd_opens==fd_closes);
    puts("ALL CHECKS PASSED");
}
