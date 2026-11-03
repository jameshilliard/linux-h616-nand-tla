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
