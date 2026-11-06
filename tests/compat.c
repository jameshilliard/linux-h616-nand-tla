/* SPDX-License-Identifier: GPL-2.0-or-later */
static void compatibility(void)
{
    unsigned cases=0;
    for (unsigned variable=0;variable<2;variable++)
    for (unsigned steps=1;steps<=(variable?32U:16U);steps++)
    for (unsigned parity=28;parity<=140;parity+=2) {
        struct caps c=caps(variable); controller.caps=&c;
        struct nand_chip n=chip(steps,parity,steps*(parity+32)+7);
        if (variable) assert(!sunxi_nfc_maximize_user_data(&n,n.mtd.oobsize,parity,steps));
        for (unsigned i=0;i<=steps;i++) {
            struct mtd_oob_region old={0}, now={0};
            assert(old_sunxi_get_oob_offset(&n,&n.ecc,i)==sunxi_get_oob_offset(&n,&n.ecc,i));
            assert(old_sunxi_nand_ooblayout_ecc(&n.mtd,i,&old)==sunxi_nand_ooblayout_ecc(&n.mtd,i,&now));
            assert(old.offset==now.offset && old.length==now.length);
            assert(old_sunxi_nand_ooblayout_free(&n.mtd,i,&old)==sunxi_nand_ooblayout_free(&n.mtd,i,&now));
            assert(old.offset==now.offset && old.length==now.length);
        }
        free(n.user_data_bytes); cases++;
    }
    for (unsigned step=512;step<=1024;step+=512)
    for (unsigned ppb=32;ppb<=256;ppb*=2)
    for (unsigned page=0;page<512;page++) {
        struct nand_chip n=chip(4,56,256); n.ecc.size=step; n.mtd.ecc_step_size=step;
        n.mtd.erasesize=n.mtd.writesize*ppb;
        for (unsigned ecc=0;ecc<2;ecc++)
            assert(old_sunxi_nfc_randomizer_state(&n,page,ecc)==sunxi_nfc_randomizer_state(&n,page,ecc));
        assert(_cal_random_seed(page%ppb)==sunxi_nfc_randomizer_state(&n,page,false));
        cases++;
    }
    for (unsigned steps=1;steps<=32;steps++) {
        struct caps c=caps(true); controller.caps=&c;
        struct nand_chip n=chip(steps,56,steps*60+32);
        struct nand_chip_info info={.sector_cnt_per_page=steps*2};
        u32 regs[32]={0}, lengths[4]={0};
        u8 input[128], result[128], linux_input[128], linux_result[128];
        struct nand_controller_info bsp={.nreg={regs,lengths}};
        bsp_spare_capacity(&info);
        assert(!TEST_VENDOR_OOB_OPS(true)->init_user_data(&n,steps));
        assert(info.sdata_bytes_per_page==sunxi_nfc_user_data_sz(&n,0));
        assert(!ndfc_set_user_data_len_cfg(&bsp,info.sdata_bytes_per_page));
        assert(!ndfc_set_user_data_len(&bsp));
        sunxi_nfc_set_user_data_len_dma(&n,0,steps);
        assert(!memcmp(lengths,controller.regs+c.reg_user_data_len,sizeof(lengths)));
        for (unsigned j=0;j<sizeof(input);j++) input[j]=j*29+steps;
        assert(!ndfc_set_spare_data(&bsp,input,info.sdata_bytes_per_page));
        /* Linux -> BSP and BSP -> Linux, including non-FF bad-block markers. */
        sunxi_nfc_user_data_to_buf(regs[0],linux_result);
        for (unsigned j=0;j<info.sdata_bytes_per_page;j+=4) {
            sunxi_nfc_user_data_to_buf(regs[j/4],linux_result+j);
            regs[j/4]=sunxi_nfc_buf_to_user_data(input+j);
            memcpy(linux_input+j,input+j,4);
        }
        assert(!memcmp(linux_result,input,info.sdata_bytes_per_page));
        assert(!ndfc_get_spare_data(&bsp,result,info.sdata_bytes_per_page));
        assert(!memcmp(result,linux_input,info.sdata_bytes_per_page));
        assert(ndfc_get_spare_data(&bsp,NULL,4)==ERR_NO_29);
        assert(ndfc_set_spare_data(&bsp,NULL,4)==ERR_NO_28);
        /* Demonstrate that generic H6 per-step packing is NOT the BSP format. */
        assert(!ndfc_set_user_data_len_cfg_4bytesper1k(&bsp,steps*4));
        assert(!ndfc_set_user_data_len(&bsp));
        assert((memcmp(lengths,controller.regs+c.reg_user_data_len,sizeof(lengths))==0)==(steps==1));
        free(n.user_data_bytes); cases++;
    }
    printf("cross-version/BSP: %u layout, seed and bidirectional packing cases\n",cases);
}
