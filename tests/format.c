/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Stored-byte oracle. The hardware ECC encoder remains abstract: distinct
 * synthetic parity bytes test representation/offsets, not BCH correction. */
static void format_roundtrip(void)
{
    static u8 image[40000], main_original[32768], original[4096], decoded[4096], saved[4096];
    const unsigned pages[]={0,1,63,64,127,128,255};
    unsigned runs=0;
    for (unsigned variable=0;variable<2;variable++)
    for (unsigned format=0;format<3;format++)
    for (unsigned wi=0;wi<2;wi++) for (unsigned ri=0;ri<2;ri++)
    for (unsigned pi=0;pi<ARRAY_SIZE(pages);pi++) {
        unsigned page=pages[pi];
        bool random=format!=0, vendor=format==2;
        struct caps c=caps(variable); controller.caps=&c;
        struct nand_chip n=chip(6,56,6*88+13);
        u8 user[6]={32,32,32,32,16,0};
        if (vendor) { memset(user,0,sizeof(user)); user[0]=16; }
        n.user_data_bytes=variable?user:NULL;
        n.options=random?NAND_NEED_SCRAMBLING:0;
        n.oob_ops=vendor?TEST_VENDOR_OOB_OPS(variable):TEST_NORMAL_OOB_OPS(variable,random);
        unsigned tail=sunxi_get_oob_offset(&n,&n.ecc,n.ecc.steps);
        unsigned total=n.mtd.writesize+n.mtd.oobsize;
        memset(image,255,sizeof(image));
        for (unsigned j=0;j<n.mtd.writesize;j++) main_original[j]=j*17+(j>>8)*11+page;
        for (unsigned j=0;j<n.mtd.oobsize;j++) original[j]=j*29+(j>>8)*13+page*7;
        original[0]=0; original[1]=0xa5; /* exercise non-erased BBM, not only FF */
        u16 data_seed=bit_reference(sunxi_nfc_randomizer_state(&n,page,false),15);
        u16 spare_seed=sunxi_nfc_randomizer_state(&n,page,true);
        /* Source batching and register packing decide what reaches hardware. */
        for (unsigned first=0;first<n.ecc.steps;) {
            unsigned batch=wi?sunxi_nfc_dma_batch_steps(&n,first,n.ecc.steps):1;
            memset(controller.regs+c.reg_user_data,0x6d,variable?128:64);
            if (wi) sunxi_nfc_set_user_data_len_dma(&n,first,batch);
            unsigned reg=0;
            for (unsigned k=first;k<first+batch;k++) {
                unsigned len=sunxi_nfc_user_data_sz(&n,k);
                unsigned off=sunxi_get_oob_offset(&n,&n.ecc,k);
                if (!wi) sunxi_nfc_set_user_data_len_pio(&controller,len);
                sunxi_nfc_hw_ecc_set_prot_oob_bytes(&n,original+off,reg,!k,page,len);
                u16 state=spare_seed;
                for (unsigned j=0;j<len;j++) {
                    u32 word=readl(controller.regs+c.reg_user_data+(reg+j/4)*4);
                    u8 b=word>>((j%4)*8);
                    image[n.mtd.writesize+off+j]=b^(random?(u8)state:0);
                    state=bit_reference(state,8);
                }
                /* Main data uses the per-step page-seed stream. */
                memcpy(image+k*n.ecc.size,main_original+k*n.ecc.size,n.ecc.size);
                if (random) sunxi_nfc_randomize_buf(data_seed,image+k*n.ecc.size,n.ecc.size);
                state=data_seed;
                for (unsigned j=0;j<n.ecc.size;j++) {
                    assert(image[k*n.ecc.size+j]==(u8)(main_original[k*n.ecc.size+j]^(random?(u8)state:0)));
                    state=bit_reference(state,8);
                }
                /* Parity is opaque, but every byte/step has a distinct value. */
                for (unsigned j=0;j<n.ecc.bytes;j++) image[n.mtd.writesize+off+len+j]=k*37+j*19+page;
                reg+=len/4;
            }
            first+=batch;
        }
        memcpy(image+n.mtd.writesize+tail,original+tail,n.mtd.oobsize-tail);
        if (random) sunxi_nfc_randomize_buf(data_seed,image+n.mtd.writesize+tail,n.mtd.oobsize-tail);
        if (!vendor) assert(!memcmp(image+n.mtd.writesize,original,2));
        /* Throw away all device registers: reads must use the stored image. */
        memset(controller.regs,0xc3,sizeof(controller.regs));
        memset(decoded,0xa5,sizeof(decoded));
        for (unsigned first=0;first<n.ecc.steps;) {
            unsigned batch=ri?sunxi_nfc_dma_batch_steps(&n,first,n.ecc.steps):1,reg=0;
            for (unsigned k=first;k<first+batch;k++) {
                unsigned len=sunxi_nfc_user_data_sz(&n,k),off=sunxi_get_oob_offset(&n,&n.ecc,k);
                u8 decoded_hw[32]; u16 state=spare_seed;
                for (unsigned j=0;j<len;j++) {
                    decoded_hw[j]=image[n.mtd.writesize+off+j]^(random?(u8)state:0);
                    state=bit_reference(state,8);
                }
                for (unsigned j=0;j<len;j+=4)
                    writel(sunxi_nfc_buf_to_user_data(decoded_hw+j),controller.regs+c.reg_user_data+(reg+j/4)*4);
                sunxi_nfc_hw_ecc_get_prot_oob_bytes(&n,decoded+off,reg,!k,page,len);
                assert(!memcmp(decoded+off,original+off,len));
                reg+=len/4;
                state=data_seed;
                for (unsigned j=0;j<n.ecc.size;j++) {
                    assert((u8)(image[k*n.ecc.size+j]^(random?(u8)state:0))==main_original[k*n.ecc.size+j]);
                    state=bit_reference(state,8);
                }
            }
            first+=batch;
        }
        if (vendor) {
            memcpy(saved,decoded,sizeof(saved)); n.oob_poi=decoded;
            format_image=image; format_image_len=total; fail_raw_at=0; raw_calls=0;
            assert(!sunxi_nfc_allwinner_oob_read_unprotected(&n,ri,page));
            for (unsigned k=0;k<n.ecc.steps;k++) {
                unsigned len=sunxi_nfc_user_data_sz(&n,k),off=sunxi_get_oob_offset(&n,&n.ecc,k);
                assert(!memcmp(decoded+off,saved+off,len));
                u16 state=bit_reference(spare_seed,len*8+15);
                for (unsigned j=0;j<n.ecc.bytes;j++) {
                    u8 physical=image[n.mtd.writesize+off+len+j];
                    assert(decoded[off+len+j]==(u8)(physical^(ri?0:(u8)state)));
                    state=bit_reference(state,8);
                }
            }
            assert(!memcmp(decoded+tail,original+tail,n.mtd.oobsize-tail));
            assert(decoded[n.mtd.oobsize]==0xa5);
            format_image=NULL;
        }
        runs++;
    }
    printf("persistent format: %u PIO/DMA writer-reader combinations; raw image, BBM, main/protected OOB, parity and tail checked\n",runs);
}
