/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Full PIO page callbacks, not an arbitrary direct-buffer invocation. NAND
 * contents and ECC status remain a fixture; this is not a hardware test. */
void count_boundary_tests(void)
{
    unsigned runs=0;
    for(unsigned write=0;write<2;write++) {
        driver_reset(0,true); /* actual attach accepts 2 KiB + 2 KiB OOB */
        struct nand_chip *n=&persistent_chip;
        unsigned off=sunxi_get_oob_offset(n,&n->ecc,n->ecc.steps);
        unsigned tail=B_SPARE-off, physical=tail%1024 ? tail%1024 : 1024;
        assert(tail>1024 && tail<8192 && physical<tail);
        u8 data[B_PAGE], oob[B_SPARE];
        for(unsigned i=0;i<B_PAGE;i++) data[i]=(i*17+(i>>8))&255;
        for(unsigned i=0;i<B_SPARE;i++) oob[i]=(i*31+(i>>8))&255;
        if(!write) {
            memcpy(flash[0][0],data,B_PAGE);
            memcpy(flash[0][0]+B_PAGE,oob,B_SPARE);
            memset(data,0xa5,sizeof(data)); memset(oob,0xa5,sizeof(oob));
        }
        unsigned corrected=0,failed=0;
        int ret=driver_io(write,false,false,0,0,data,oob,true,&corrected,&failed);
        assert(!ret && !failed);
        /* The tail starts correctly, but successful completion does not mean
         * that the count requested by the software was transferred. */
        assert(!memcmp(flash[0][0]+B_PAGE+off,oob+off,physical));
        assert(memcmp(flash[0][0]+B_PAGE+off,oob+off,tail));
        if(write) assert(!memchr_inv(flash[0][0]+B_PAGE+off+physical,255,tail-physical));
        printf("SOURCE CONTRACT WITNESS: page %s accepts OOB tail %u, transfers %u, returns success\n",
               write ? "write" : "read",tail,physical);
        driver_finish(); runs++;
    }
    printf("ALL CHECKS PASSED: %u actual-attach/page/extra-OOB/parser/MMIO count witnesses\n",runs);
}
