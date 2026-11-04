/* SPDX-License-Identifier: GPL-2.0-or-later */
static void reset_params(unsigned page,unsigned spare)
{
    memset(copies,0,sizeof(copies)); memset(extended,0,sizeof(extended));
    struct nand_onfi_params *p=&copies[0];
    memcpy(p->sig,"ONFI",4); p->revision=ONFI_VERSION_2_3;
    memcpy(p->manufacturer,"fixture",7); memcpy(p->model,"parameter fixture",17);
    p->byte_per_page=page; p->spare_bytes_per_page=spare;
    p->pages_per_block=96; p->blocks_per_lun=1500; p->lun_count=2;
    p->bits_per_cell=1; p->ecc_bits=16; p->num_of_param_pages=3;
    p->opt_cmd=ONFI_OPT_CMD_SET_GET_FEATURES|ONFI_OPT_CMD_READ_CACHE;
    p->crc=onfi_crc16(ONFI_CRC_BASE,(u8 *)p,254);
    copies[1]=copies[2]=*p;
    allocations=fail_alloc=transport_count=fail_transport=0; has_signature=true;
}
int main(void)
{
    unsigned cases=0,attached=0,rejected=0;
    assert(sizeof(struct nand_onfi_params)==256);
    for (unsigned route=0;route<3;route++)
    for (unsigned pattern=0;pattern<6;pattern++)
    for (unsigned page=2048;page<=8192;page*=2)
    for (unsigned spare=64;spare<=512;spare*=2) {
        struct nand_controller nc={.supported_op.data_only_read=route==1};
        struct nand_chip c={.controller=&nc}; has_exec=route!=2;
        reset_params(page,spare);
        if (pattern==1) copies[0].model[1]^=1;
        if (pattern==2) { copies[0].model[1]^=1; copies[1].model[2]^=2; }
        if (pattern==3) for(unsigned i=0;i<3;i++) copies[i].model[i]^=1; /* majority repairs */
        if (pattern==4) for(unsigned i=0;i<3;i++) copies[i].model[0]^=1;
        if (pattern==5) { copies[0].model[1]^=1; fail_transport=2; }
        int r=nand_onfi_detect(&c);
        assert(r==(pattern<4));
        if (r==1) {
            assert(c.mtd.writesize==page && c.mtd.oobsize==spare);
            assert(c.base.mem.pages_per_eraseblock==64 && c.base.mem.eraseblocks_per_lun==1024);
            assert(c.base.req.step_size==512 && c.base.req.strength==16);
            for(unsigned vendor=0;vendor<2;vendor++) for(unsigned var=0;var<2;var++) for(unsigned dma=0;dma<2;dma++) {
                int a=driver_attach_check(c.mtd.writesize,c.mtd.oobsize,c.base.req.step_size,c.base.req.strength,vendor,var,dma);
                if(!a) attached++; else rejected++;
            }
            free(c.parameters.model); free(c.parameters.onfi);
        }
        cases++;
    }
    for(unsigned error=0;error<8;error++) {
        struct nand_controller nc={0}; struct nand_chip c={.controller=&nc};
        reset_params(2048,256);
        struct nand_onfi_params *p=&copies[0];
        p->ecc_bits=255; p->features=ONFI_FEATURE_EXT_PARAM_PAGE|ONFI_FEATURE_16_BIT_BUS;
        p->ext_param_page_length=sizeof(extended)/16;
        struct onfi_ext_param_page *ep=(void *)extended;
        memcpy(ep->sig,"EPPS",4); ep->sections[0]=(struct onfi_ext_section){.type=2,.length=1};
        struct onfi_ext_ecc_info *ecc=(void *)(ep+1); ecc->codeword_size=10; ecc->ecc_bits=32;
        if(error==1) ep->sig[0]=0;
        if(error==2) ep->sections[0].length=255;
        if(error==3) ep->sections[0].length=0;
        if(error==4) memset(ep->sections,0,sizeof(ep->sections));
        if(error==5) ecc->codeword_size=0;
        ep->crc=onfi_crc16(ONFI_CRC_BASE,extended+2,sizeof(extended)-2);
        if(error==6) ep->crc^=1;
        if(error==7) fail_transport=2;
        p->crc=onfi_crc16(ONFI_CRC_BASE,(u8 *)p,254); copies[1]=copies[2]=*p;
        assert(nand_onfi_detect(&c)==1 && c.options&NAND_BUSWIDTH_16);
        assert((c.base.req.step_size==1024)==!error);
        assert(driver_attach_check(c.mtd.writesize,c.mtd.oobsize,c.base.req.step_size,c.base.req.strength,true,true,true)==(error?-EINVAL:0));
        free(c.parameters.model); free(c.parameters.onfi); cases++;
    }
    for(unsigned fail=1;fail<=3;fail++) {
        struct nand_controller nc={0}; struct nand_chip c={.controller=&nc};
        reset_params(2048,256); fail_alloc=fail;
        assert(nand_onfi_detect(&c)==-ENOMEM); cases++;
    }
    for(unsigned id=0;id<256;id++) {
        struct nand_chip c={0}; c.id.data[3]=id; c.id.data[2]=id;
        nand_decode_ext_id(&c);
        assert(c.mtd.writesize==(1024U<<(id&3)));
        assert(c.mtd.oobsize==(8U<<((id>>2)&1))*(c.mtd.writesize/512));
        driver_attach_check(c.mtd.writesize,c.mtd.oobsize,512,8,false,false,false); cases++;
    }
    for(unsigned period=1;period<=8;period++) {
        u8 id[8]; for(unsigned i=0;i<8;i++) id[i]=i%period+1;
        assert(nand_id_len(id,8)==period);
    }
    u8 zero[8]={0}; assert(!nand_id_len(zero,8));
    u8 trailing[8]={1,2,3,0}; assert(nand_id_len(trailing,8)==3);
    struct nand_chip legacy={0}; struct nand_flash_dev type={.pagesize=512,.erasesize=16384};
    nand_decode_id(&legacy,&type); assert(legacy.mtd.oobsize==16);
    printf("Discovery-to-attach: %u scenarios; %u accepted/%u rejected driver profiles\n",cases,attached,rejected);
    puts("ALL CHECKS PASSED");
}
