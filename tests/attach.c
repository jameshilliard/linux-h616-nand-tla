/* SPDX-License-Identifier: GPL-2.0-or-later */
static void attach_geometry(void)
{
    static const unsigned pages[] = {512, 1024, 2048, 4096, 16384, 32768, 65536};
    static const unsigned sizes[] = {256, 512, 1024, 2048};
    static const unsigned strengths[] = {1, 8, 16, 24, 32, 40, 64, 80, 81};
    static const unsigned spares[] = {16, 64, 128, 224, 512, 1024, 4096, 8192};
    unsigned accepted = 0, rejected = 0, overlarge = 0, core_fallback = 0;
    flow.enabled = false;
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned vendor = 0; vendor < 2; vendor++)
    for (unsigned maximize = 0; maximize < 2; maximize++)
    for (unsigned p = 0; p < ARRAY_SIZE(pages); p++)
    for (unsigned e = 0; e < ARRAY_SIZE(sizes); e++)
    for (unsigned s = 0; s < ARRAY_SIZE(strengths); s++)
    for (unsigned o = 0; o < ARRAY_SIZE(spares); o++) {
        struct caps c = caps(variable);
        c.ecc_strengths = variable ? sunxi_ecc_strengths_h6 : sunxi_ecc_strengths_a10;
        c.nstrengths = variable ? ARRAY_SIZE(sunxi_ecc_strengths_h6) : ARRAY_SIZE(sunxi_ecc_strengths_a10);
        c.ecc_mode_mask = variable ? GENMASK(15, 8) : GENMASK(15, 12);
        c.has_ecc_block_512 = !variable;
        controller.caps = &c;
        controller.use_mdma = true;
        controller.mdma_desc = variable ? controller.regs : NULL;
        struct sunxi_nfc_oob_ops ops = {
            .min_user_data = vendor ? sunxi_nfc_allwinner_oob_min_user_data : sunxi_nfc_oob_min_user_data,
            .init_user_data = vendor ? sunxi_nfc_allwinner_oob_init_user_data : sunxi_nfc_oob_init_user_data,
        };
        struct nand_chip nand = chip(1, 0, spares[o]);
        nand.oob_ops = &ops;
        nand.mtd.writesize = pages[p];
        nand.ecc.size = sizes[e];
        nand.ecc.strength = strengths[s];
        device_config.ecc.user_conf.flags = maximize ? NAND_ECC_MAXIMIZE_STRENGTH : 0;
        int ret = sunxi_nand_hw_ecc_ctrl_init(&nand, &nand.ecc, NULL);
        unsigned normalized_size = maximize ? 1024 :
            sizes[e] == 512 && pages[p] > 512 ? 1024 : sizes[e];
        if (pages[p] / normalized_size > c.max_ecc_steps) {
            assert(ret < 0);
            overlarge++;
        }
        if (!ret) {
            unsigned n = pages[p] / nand.ecc.size;
            if (nand.ecc.size > (int)pages[p]) {
                /* nand_scan_tail() switches to software ECC before any hardware
                 * page callback can run. Vendor format rejects zero steps. */
                assert(!vendor && !variable && pages[p] == 512 && !n);
                core_fallback++;
                free(nand.user_data_bytes);
                continue;
            }
            assert(n && n <= c.max_ecc_steps);
            assert(nand.ecc.size == 1024 || (pages[p] == 512 && c.has_ecc_block_512));
            assert(nand.ecc.bytes == ALIGN(DIV_ROUND_UP(nand.ecc.strength * 14, 8), 2));
            assert((unsigned)sunxi_get_oob_offset(&nand, &nand.ecc, n) <= spares[o]);
            assert(sunxi_nfc_user_data_sz(&nand, 0) >= 4);
            if (pages[p] > 512) {
                assert(nand.ecc.read_page == sunxi_nfc_hw_ecc_read_page_dma);
                assert(nand.ecc.write_page == sunxi_nfc_hw_ecc_write_page_dma);
                assert(nand.options & NAND_USES_DMA);
                if (variable) assert(nand.buf_align == 4);
            } else {
                assert(nand.ecc.read_page == sunxi_nfc_hw_ecc_read_page);
                assert(!(nand.options & NAND_USES_DMA));
            }
            assert(nand.ecc.write_oob == sunxi_nfc_hw_ecc_write_oob);
            accepted++;
        } else {
            rejected++;
        }
        free(nand.user_data_bytes);
    }
    printf("source-extracted ECC init: %u accepted, %u rejected, %u core-fallback cases (%u over-limit geometries)\n",
           accepted, rejected, core_fallback, overlarge);
}

static unsigned whole_page_calls;
static int whole_page_fixture(struct nand_chip *nand,u8 *buf,int oob,int page)
{
    assert(!oob && page==17 && buf==main_data);
    whole_page_calls++;
    memset(buf,0x39,nand->mtd.writesize);
    return 3;
}
static void attach_policy(void)
{
    unsigned runs=0;
    flow.enabled=false;
    device_config.ecc.user_conf.flags=0;
    for(unsigned variable=0;variable<2;variable++)
    for(unsigned vendor=0;vendor<2;vendor++)
    for(unsigned scrambled=0;scrambled<2;scrambled++)
    for(unsigned engine=0;engine<4;engine++)
    for(unsigned inherit=0;inherit<3;inherit++)
    for(unsigned allocfail=0;allocfail<2;allocfail++) {
        struct caps c=caps(variable);
        c.ecc_strengths=variable ? sunxi_ecc_strengths_h6 : sunxi_ecc_strengths_a10;
        c.nstrengths=variable ? ARRAY_SIZE(sunxi_ecc_strengths_h6) : ARRAY_SIZE(sunxi_ecc_strengths_a10);
        c.ecc_mode_mask=variable ? GENMASK(15,8) : GENMASK(15,12);
        c.has_ecc_block_512=!variable;
        controller.caps=&c; controller.use_mdma=true;
        struct nand_chip n=chip(2,56,224);
        n.options=scrambled ? NAND_NEED_SCRAMBLING : 0;
        n.bbt_options=NAND_BBT_USE_FLASH;
        n.ecc.engine_type=engine;
        n.base.requirements=(struct nand_ecc_props){1024,32};
        if(inherit) n.ecc.size=0;
        if(inherit==2) n.base.requirements.strength=0;
        vendor_property=vendor; allocation_failure=allocfail;
        int ret=sunxi_nand_attach_chip(&n);
        bool allowed=inherit!=2 && (vendor ? engine==0 : engine<3) &&
            !(variable && engine==0 && allocfail);
        assert((ret==0)==allowed);
        assert(n.bbt_options&NAND_BBT_NO_OOB);
        assert(n.options&NAND_SUBPAGE_READ);
        if(!ret) {
            if(vendor) {
                assert(n.oob_ops==&sunxi_nfc_allwinner_oob_ops);
                assert((n.options&(NAND_NEED_SCRAMBLING|NAND_NO_SUBPAGE_WRITE))==
                    (NAND_NEED_SCRAMBLING|NAND_NO_SUBPAGE_WRITE));
                assert(n.ecc.read_subpage==sunxi_nfc_allwinner_oob_read_subpage);
            } else {
                assert(n.oob_ops==(scrambled ? &sunxi_nfc_scrambled_plain_bbm_oob_ops : &sunxi_nfc_plain_oob_ops));
            }
        }
        free(n.user_data_bytes); runs++;
    }
    allocation_failure=vendor_property=false;
    struct nand_chip n=chip(2,56,224);
    n.ecc.read_page=whole_page_fixture;
    memset(main_data,0xa5,sizeof(main_data));
    assert(sunxi_nfc_allwinner_oob_read_subpage(&n,1025,1,main_data,17)==3);
    assert(whole_page_calls==1 && !memchr_inv(main_data,0x39,2048) && main_data[2048]==0xa5);
    n.mtd.ecc_stats.failed=UINT32_MAX;
    n.mtd.ecc_stats.corrected=UINT32_MAX-1;
    unsigned maxflips=0;
    sunxi_nfc_hw_ecc_update_stats(&n,&maxflips,-EBADMSG);
    sunxi_nfc_hw_ecc_update_stats(&n,&maxflips,3);
    assert(n.mtd.ecc_stats.failed==0 && n.mtd.ecc_stats.corrected==1 && maxflips==3);
    printf("actual attach/format/allocation policy: %u cases; whole-page vendor subread and 32-bit ECC counter wrap\n",runs);
}

static void metadata_identity(void)
{
    unsigned runs=0;
    for(unsigned variable=0;variable<2;variable++)
    for(unsigned scrambled=0;scrambled<2;scrambled++)
    for(unsigned vendor=0;vendor<2;vendor++)
    for(unsigned page=0;page<128;page+=7) {
        struct caps c=caps(variable); controller.caps=&c;
        struct nand_chip n=chip(6,56,6*88+13);
        u8 user[6]={32,16,4,0,0,0};
        if(vendor) { memset(user,0,sizeof(user)); user[0]=16; }
        n.user_data_bytes=variable ? user : NULL;
        n.oob_ops=vendor ? &sunxi_nfc_allwinner_oob_ops : scrambled ?
            &sunxi_nfc_scrambled_plain_bbm_oob_ops : &sunxi_nfc_plain_oob_ops;
        u8 original[1024],decoded[1024],flash[1024];
        memset(flash,0xff,sizeof(flash)); memset(decoded,0xa5,sizeof(decoded));
        for(unsigned i=0;i<sizeof(original);i++) original[i]=(i*23+(i>>8)*7+page*11)&255;
        unsigned cursor=0, reg=0;
        for(unsigned step=0;step<6;step++) {
            unsigned len=variable ? user[step] : 4;
            sunxi_nfc_hw_ecc_set_prot_oob_bytes(&n,original+cursor,reg,!step,page,len);
            u16 seed=sunxi_nfc_randomizer_state(&n,page,true);
            for(unsigned i=0;i<len;i++) {
                unsigned word=readl(controller.regs+c.reg_user_data+(reg+i/4)*4);
                u8 value=word>>((i%4)*8);
                u8 mask=(u8)seed;
                /* The hardware transform is independent of the driver's BBM compensation. */
                flash[cursor+i]=value^(scrambled || vendor ? mask : 0);
                if(!step && i<2 && scrambled && !vendor)
                    assert(flash[cursor+i]==original[cursor+i]);
                seed=bit_reference(seed,8);
            }
            sunxi_nfc_hw_ecc_get_prot_oob_bytes(&n,decoded+cursor,reg,!step,page,len);
            assert(!memcmp(decoded+cursor,original+cursor,len));
            for(unsigned i=0;i<56;i++) assert(decoded[cursor+len+i]==0xa5);
            cursor+=len+56; reg+=len/4;
        }
        assert(decoded[cursor]==0xa5 && flash[cursor]==0xff);
        runs++;
    }
    printf("protected metadata identity: %u page/layout/format cases; distinct bytes, BBM and parity/tail sentinels\n",runs);
}
