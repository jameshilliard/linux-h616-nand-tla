/* SPDX-License-Identifier: GPL-2.0-or-later */
int main(void)
{
    struct caps *const variants[] = { &a10_caps, &a23_caps, &h616_caps };
    const unsigned pages[] = { 512, 2048, 4096 };
    const unsigned spares[] = { 16, 32, 64, 128, 256 };
    unsigned tested = 0, parity_witnesses[2] = {0};

    for (unsigned v = 0; v < ARRAY_SIZE(variants); v++)
    for (unsigned p = 0; p < ARRAY_SIZE(pages); p++)
    for (unsigned s = 0; s < ARRAY_SIZE(spares); s++)
    for (unsigned fw = 0; fw <= HAS_FIRMWARE; fw++)
    for (unsigned wide = 0; wide <= 1; wide++)
    for (unsigned soft = 0; soft <= 1; soft++) {
        unsigned page = pages[p], spare = spares[s], steps = page == 512 ? 1 : page / 1024;
        unsigned required = steps * (28 + 4); /* BCH16 and at least one word per step */
        if (v == 2 && !fw)
            required = steps * 28 + 4; /* ordinary variable-length format */
        struct nand_chip n = { .controller = &controller, .mtd = { .writesize = page, .oobsize = spare },
                              .ecc = { .engine_type = soft ? NAND_ECC_ENGINE_TYPE_SOFT :
                                                          NAND_ECC_ENGINE_TYPE_ON_HOST,
                                       .size = page == 512 ? 512 : 1024, .strength = 16 } };
        controller.caps = variants[v];
        vendor_property = fw;
        if (wide) n.options |= NAND_BUSWIDTH_16;
        nand_decode_bbm_options(&n);
        unsigned detected = n.badblockpos;
        assert(detected == (page == 512 && !wide ? 5 : 0));
        int ret = sunxi_nand_attach_chip(&n);
        bool supported = soft ? !fw : (spare >= required && (page != 512 || v < 2));
        if (fw && detected && !UNGUARDED) {
            assert(ret == -EINVAL && !n.allwinner_oob && !n.ecc.read_subpage);
            assert(!(n.options & NAND_NEED_SCRAMBLING));
            supported = false;
        }
        if (!fw && !soft && detected >= 2 && !PLAIN_UNGUARDED)
            supported = false;
        assert(n.badblockpos == detected && (ret == 0) == supported);
#if HAS_FIRMWARE
        if (!ret && fw) {
            assert(n.allwinner_oob && n.ecc.read_subpage == sunxi_nfc_allwinner_oob_read_subpage);
        }
#endif
        if (!ret && !soft) {
            struct mtd_oob_region parity, available;
            n.ecc.steps = page / n.ecc.size; /* nand_scan_tail() */
            assert(!sunxi_nand_ooblayout_ecc(&n.mtd, 0, &parity));
            assert(!sunxi_nand_ooblayout_free(&n.mtd, 0, &available));
            assert(available.offset == 2 && available.offset + available.length <= parity.offset);
            if (detected >= parity.offset && detected < parity.offset + parity.length)
                parity_witnesses[fw]++;
        }
        if (!fw) assert(!n.allwinner_oob);
        tested++;
    }
    assert(parity_witnesses[0] == (PLAIN_UNGUARDED ? 8 : 0));
    assert(parity_witnesses[1] == (UNGUARDED ? 8 : 0));
    printf("%u attach cases; accepted markers in parity: ordinary %u, firmware %u\n",
           tested, parity_witnesses[0], parity_witnesses[1]);
    return 0;
}
