/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real BCH decoder outputs and independent physical images from run_codec.py.
 * MMIO remains a fixture. No hardware BCH implementation is assumed proven. */
static void codec_bridge(void)
{
    const char *path=getenv("NAND_CODEC_VECTORS");
    if (!path) return;
    FILE *f=fopen(path,"rb"); assert(f);
    unsigned runs=0; u32 h[9];
    size_t header_words;
    while ((header_words=fread(h,4,9,f))==9) {
        unsigned step=h[1],steps=h[2],parity=h[3],variable=h[4],vendor=h[5];
        unsigned random=h[6],page=h[7],spare=h[8],size=step*steps;
        assert(h[0]==0x4e414e44 && steps<=32 && size<=32768 && spare<4096);
        u8 original[32768],logical[4096],physical[40000],decoded_hw[34000];
        u8 user[32]={0}, output[4096], work[32768];
        unsigned protected=variable?(vendor?min(steps*4,16):steps*32):steps*4;
        assert(fread(original,1,size,f)==size && fread(logical,1,spare,f)==spare);
        assert(fread(physical,1,size+spare,f)==size+spare);
        assert(fread(decoded_hw,1,size+protected,f)==size+protected);
        struct caps c=caps(variable); controller.caps=&c;
        struct nand_chip n=chip(steps,parity,spare);
        n.ecc.size=step; n.mtd.ecc_step_size=step; n.mtd.writesize=size; n.mtd.erasesize=size*64;
        n.options=random?NAND_NEED_SCRAMBLING:0;
        if (variable) {
            if (vendor) user[0]=min(steps*4,16);
            else memset(user,32,steps);
            n.user_data_bytes=user;
        }
        n.oob_ops=vendor?TEST_VENDOR_OOB_OPS(variable):TEST_NORMAL_OOB_OPS(variable,random);
        for (unsigned dma=0;dma<(size>512?2U:1U);dma++) {
            unsigned decoded_off=0;
            memset(output,0xa5,sizeof(output));
            for (unsigned first=0;first<steps;) {
                unsigned batch=dma?sunxi_nfc_dma_batch_steps(&n,first,steps):1, reg=0;
                if (dma) sunxi_nfc_set_user_data_len_dma(&n,first,batch);
                for (unsigned k=first;k<first+batch;k++) {
                    unsigned len=sunxi_nfc_user_data_sz(&n,k),off=sunxi_get_oob_offset(&n,&n.ecc,k);
                    if (!dma) sunxi_nfc_set_user_data_len_pio(&controller,len);
                    sunxi_nfc_hw_ecc_set_prot_oob_bytes(&n,logical+off,reg,!k,page,len);
                    for (unsigned j=0;j<len;j+=4) {
                        u8 bytes[4];
                        sunxi_nfc_user_data_to_buf(readl(controller.regs+c.reg_user_data+(reg+j/4)*4),bytes);
                        /* Corrected real BCH output must equal what the driver
                         * supplied before randomization, including plain BBM. */
                        assert(!memcmp(bytes,decoded_hw+decoded_off+step+j,4));
                        writel(sunxi_nfc_buf_to_user_data(decoded_hw+decoded_off+step+j),
                               controller.regs+c.reg_user_data+(reg+j/4)*4);
                    }
                    sunxi_nfc_hw_ecc_get_prot_oob_bytes(&n,output+off,reg,!k,page,len);
                    assert(!memcmp(output+off,logical+off,len));
                    assert(!memcmp(decoded_hw+decoded_off,original+k*step,step));
                    /* Independently formed pristine media must agree with the
                     * driver's main-data and protected-OOB XOR convention. */
                    memcpy(work,physical+k*step,step);
                    if (random) sunxi_nfc_randomize_buf(bit_reference(sunxi_nfc_randomizer_state(&n,page,false),15),work,step);
                    assert(!memcmp(work,original+k*step,step));
                    memcpy(work,physical+size+off,len);
                    if (random) sunxi_nfc_randomize_buf(sunxi_nfc_randomizer_state(&n,page,true),work,len);
                    assert(!memcmp(work,decoded_hw+decoded_off+step,len));
                    decoded_off+=step+len; reg+=len/4;
                }
                first+=batch;
            }
            assert(decoded_off==size+protected && output[spare]==0xa5);
            if (vendor) {
                n.oob_poi=output; format_image=physical; format_image_len=size+spare;
                fail_raw_at=0; raw_calls=0;
                assert(!sunxi_nfc_allwinner_oob_read_unprotected(&n,dma,page));
                for (unsigned k=0;k<steps;k++) {
                    unsigned len=sunxi_nfc_user_data_sz(&n,k),off=sunxi_get_oob_offset(&n,&n.ecc,k);
                    assert(!memcmp(output+off,logical+off,len));
                    u16 state=bit_reference(sunxi_nfc_randomizer_state(&n,page,true),len*8+15);
                    for (unsigned j=0;j<parity;j++) {
                        assert(output[off+len+j]==(u8)(physical[size+off+len+j]^(dma?0:(u8)state)));
                        state=bit_reference(state,8);
                    }
                }
                unsigned tail=sunxi_get_oob_offset(&n,&n.ecc,steps);
                assert(!memcmp(output+tail,logical+tail,spare-tail));
                format_image=NULL;
            }
            runs++;
        }
    }
    assert(!header_words && feof(f) && !ferror(f)); fclose(f);
    printf("real BCH/driver composition: %u PIO/DMA register/layout/randomizer round trips\n",runs);
}
