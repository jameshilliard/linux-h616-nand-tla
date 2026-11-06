/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Typed value adapter between separate host TUs; not a kernel ABI emulation. */
int driver_attach_check(unsigned page, unsigned spare, unsigned step,
                        unsigned strength, bool vendor, bool variable, bool dma)
{
    struct caps c=caps(variable);
    c.ecc_strengths=variable?sunxi_ecc_strengths_h6:sunxi_ecc_strengths_a10;
    c.nstrengths=variable?ARRAY_SIZE(sunxi_ecc_strengths_h6):ARRAY_SIZE(sunxi_ecc_strengths_a10);
    c.ecc_mode_mask=variable?GENMASK(15,8):GENMASK(15,12);
    c.has_ecc_block_512=!variable;
    controller.caps=&c; controller.use_mdma=dma;
    controller.dmac=dma?&controller:NULL; controller.mdma_desc=dma?&controller:NULL;
    struct nand_chip n=chip(1,0,spare);
    n.mtd.writesize=page; n.ecc.size=n.ecc.strength=0;
    n.ecc.engine_type=NAND_ECC_ENGINE_TYPE_ON_HOST;
    n.base.requirements=(struct nand_ecc_props){.step_size=step,.strength=strength};
    vendor_property=vendor; device_config.ecc.user_conf.flags=0;
#if SINGLE_INSTALL
    n.oob_ops=NULL;
#endif
    int ret=sunxi_nand_attach_chip(&n);
#if SINGLE_INSTALL
    assert(!n.oob_ops);
    if (!ret) n.oob_ops=sunxi_nfc_get_oob_ops(&n);
#endif
    if (!ret && n.ecc.size<=page) {
        unsigned expected_strength=step==512 && page>512?strength*2:strength;
        unsigned mode=0;
        while(mode<c.nstrengths && c.ecc_strengths[mode]<expected_strength) mode++;
        assert(mode<c.nstrengths && n.ecc.strength==c.ecc_strengths[mode]);
        unsigned expected_bytes=(((c.ecc_strengths[mode]*14+7)/8)+1)&~1U;
        assert(n.ecc.bytes==expected_bytes);
        if(n.ecc.size==512) assert(n.ecc.ecc_ctl & NFC_ECC_BLOCK_512);
        n.ecc.steps=page/n.ecc.size;
        assert(n.ecc.steps && n.ecc.steps<=c.max_ecc_steps);
        assert(sunxi_get_oob_offset(&n,&n.ecc,n.ecc.steps)<=spare);
        unsigned end=0;
        for (unsigned s=0;s<n.ecc.steps;s++) {
            struct mtd_oob_region r;
            assert(!sunxi_nand_ooblayout_ecc(&n.mtd,s,&r));
            assert(r.offset>=end && r.offset+r.length<=spare);
            end=r.offset+r.length;
        }
        assert(n.ecc.read_page && n.ecc.write_page && n.ecc.read_oob && n.ecc.write_oob);
        assert((n.ecc.read_page==sunxi_nfc_hw_ecc_read_page_dma)==(dma && page>512));
        if (vendor) assert(n.oob_ops==TEST_VENDOR_OOB_OPS(variable));
    }
    free(n.user_data_bytes);
    return ret;
}
