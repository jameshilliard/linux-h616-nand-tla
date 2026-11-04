/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Run the actual core retry loop through real vendor callbacks and exec_op. */
static unsigned retry_calls;
static int configure_retry(struct nand_chip *c, int mode)
{
    retry_calls++;
    int ret=wire_retry_setup(mode);
    if(!ret && mode) driver_faults(-1,-1);
    return ret;
}
static void core_retry_tests(void)
{
    unsigned cases=0, partial=0, nondefault=0;
    for(unsigned vendor=0;vendor<2;vendor++)
    for(int fail_mode=-1;fail_mode<=1;fail_mode++)
    for(unsigned ordinal=1;ordinal<=(vendor?1:6);ordinal++)
    for(unsigned accepted=0;accepted<2;accepted++) {
        setup(0,true); use_dma=false; retry_calls=0;
        u8 input[PAGE],output[PAGE],staged[4],active[4];
        memset(input,0x96,sizeof(input));
        struct mtd_oob_ops op={.mode=MTD_OPS_PLACE_OOB,.len=PAGE,.datbuf=input};
        assert(!nand_do_write_ops(&chip,PAGE,&op));
        wire_retry_start(vendor,fail_mode,ordinal,accepted?4:0);
        chip.read_retries=2; chip.ops.setup_read_retry=configure_retry;
        driver_faults(-1,1);
        op=(struct mtd_oob_ops){.mode=MTD_OPS_PLACE_OOB,.len=PAGE,.datbuf=output};
        int ret=nand_do_read_ops(&chip,PAGE,&op);
        unsigned applies,mode;
        media_retry_snapshot(staged,active,&applies,&mode);
        if(fail_mode<0) {
            assert(!ret && retry_calls==2 && !memcmp(input,output,PAGE));
            assert(!memcmp(staged,(u8[4]){0},4) && !memcmp(active,(u8[4]){0},4));
        } else {
            assert(ret==-ETIMEDOUT);
            assert(op.retlen==(fail_mode==0?PAGE:0));
            assert(retry_calls==(fail_mode==0?2:1));
            if(memcmp(staged,(u8[4]){0},4)) partial++;
            if(memcmp(active,(u8[4]){0},4)) nondefault++;
        }
        /* Returning an error does not itself restore the chip's parameters. */
        wire_retry_start(vendor,-1,0,0); assert(!wire_retry_setup(0));
        media_retry_snapshot(staged,active,&applies,&mode);
        assert(!memcmp(active,(u8[4]){0},4));
        driver_finish(); cases++;
    }
    printf("Core/vendor retry: %u cases; %u retained staged states; %u retained active states\n",
           cases,partial,nondefault);
}

/* Real core bounce selection and memcpy bounds, not host DMA coherency. */
static void core_copy_tests(void)
{
    _Alignas(64) u8 input[2*PAGE], output[2*PAGE+128], tx[PAGE+128], check[PAGE];
    static const unsigned lengths[]={1,63,64,65,2047,2048,2049};
    static const unsigned columns[]={0,1,2047};
    unsigned reads=0,writes=0;
    for(unsigned alignment=4;alignment<=64;alignment*=16) {
        setup(0,true); use_dma=true;
        chip.options|=NAND_USES_DMA; chip.buf_align=alignment;
        for(unsigned i=0;i<sizeof(input);i++) input[i]=(i*17+(i>>8)*3)&255;
        struct mtd_oob_ops op={.mode=MTD_OPS_PLACE_OOB,.len=sizeof(input),.datbuf=input};
        assert(!nand_do_write_ops(&chip,0,&op));
        for(unsigned offset=0;offset<64;offset++)
        for(unsigned c=0;c<ARRAY_SIZE(columns);c++)
        for(unsigned l=0;l<ARRAY_SIZE(lengths);l++) {
            unsigned column=columns[c],len=lengths[l];
            memset(output,0xa5,sizeof(output)); chip.pagecache.page=-1;
            unsigned bounced=transport_bounced,direct=transport_direct;
            op=(struct mtd_oob_ops){.mode=MTD_OPS_PLACE_OOB,.len=len,.datbuf=output+64+offset};
            assert(!nand_do_read_ops(&chip,column,&op));
            assert(op.retlen==len && !memcmp(op.datbuf,input+column,len));
            for(unsigned i=0;i<64+offset;i++) assert(output[i]==0xa5);
            for(unsigned i=64+offset+len;i<sizeof(output);i++) assert(output[i]==0xa5);
            if(!column && len==PAGE) {
                assert(transport_bounced-bounced==(offset%alignment!=0));
                assert(transport_direct-direct==(offset%alignment==0));
            }
            reads++;
        }
        for(unsigned offset=0;offset<64;offset++) {
            driver_erase(0,1);
            for(unsigned i=0;i<sizeof(tx);i++) tx[i]=(i*7+19)&255;
            unsigned bounced=transport_bounced,direct=transport_direct;
            op=(struct mtd_oob_ops){.mode=MTD_OPS_PLACE_OOB,.len=PAGE,.datbuf=tx+64+offset};
            assert(!nand_do_write_ops(&chip,0,&op) && op.retlen==PAGE);
            assert(transport_bounced-bounced==(offset%alignment!=0));
            assert(transport_direct-direct==(offset%alignment==0));
            for(unsigned i=0;i<sizeof(tx);i++) assert(tx[i]==((i*7+19)&255));
            op=(struct mtd_oob_ops){.mode=MTD_OPS_PLACE_OOB,.len=PAGE,.datbuf=check};
            assert(!nand_do_read_ops(&chip,0,&op) && !memcmp(check,tx+64+offset,PAGE));
            writes++;
        }
        driver_finish();
    }
    printf("Core byte ownership: %u reads, %u writes; every destination/source guard preserved\n",reads,writes);
}
