/* SPDX-License-Identifier: GPL-2.0-or-later */
int main(void)
{
    unsigned cases = 0, bad_reads = 0, bad_writes = 0;
    for (unsigned hw = 0; hw < 2; hw++)
    for (unsigned mode = 0; mode < 3 + HAS_VENDOR; mode++)
    for (unsigned page = 0; page < 64; page++)
    for (unsigned bad_marker = 0; bad_marker < 2; bad_marker++) {
        struct caps caps = {
            .reg_user_data = hw ? 0x80 : 0x50,
            .reg_user_data_len = hw ? 0x70 : 0,
            .random_en = hw ? (1U << 5) : (1U << 9),
            .allwinner_oob_ops = hw ? TEST_H6_VENDOR_OPS : TEST_A10_VENDOR_OPS,
            .plain_oob_ops = hw ? TEST_H6_PLAIN_OPS : TEST_A10_PLAIN_OPS,
            .scrambled_oob_ops = hw ? TEST_H6_SCRAMBLED_OPS : TEST_A10_SCRAMBLED_OPS,
        };
        struct sunxi_nfc nfc = { .caps = &caps };
        struct nand_chip n = {
            .controller = &nfc,
            .mtd = { .writesize = 16384, .erasesize = 16384 * 64,
                     .ecc_step_size = 1024 },
            .base = { .step_size = 1024, .strength = 40 },
            .options = mode == 1 ? NAND_NEED_SCRAMBLING : 0,
        };
        vendor_property = mode == 3;
        assert(!sunxi_nand_attach_chip(&n));
        if (mode == 2)
            assert(!tc58teg5dclta00_init(&n));
#if SINGLE_INSTALL
        assert(!n.oob_ops);
        unsigned final_options = n.options;
        const struct sunxi_nfc_oob_ops *ops = sunxi_nfc_get_oob_ops(&n);
        assert(!n.oob_ops); /* Lookup is pure, including after manufacturer init. */
        assert(n.options == final_options);
        assert(ops->use_randomizer == (mode != 0));
        assert(!!ops->transform_bbm == (mode == 1 || mode == 2));
        assert(geometry_ops->min_user_data == ops->min_user_data);
        assert(geometry_ops->init_user_data == ops->init_user_data);
        n.oob_ops = ops;
        assert(sunxi_nfc_get_oob_ops(&n) == ops && n.oob_ops == ops);
#elif DEFERRED_SELECTION
        /* Full probe ordering is executed independently by run_lifecycle.py. */
        sunxi_nfc_select_oob_ops(&n);
        const struct sunxi_nfc_oob_ops *ops = n.oob_ops;
        assert(ops->randomized == (mode != 0));
        assert(!!ops->transform_bbm == (mode == 1 || mode == 2));
        const struct sunxi_nfc_oob_ops *final = ops;
        sunxi_nfc_select_oob_ops(&n);
        assert(n.oob_ops == final);
#elif HAS_OPS_POLICY
        const struct sunxi_nfc_oob_ops *ops = sunxi_nfc_get_oob_ops(&n);
        assert(ops->randomized == (mode != 0));
        assert(!!ops->transform_bbm == (mode == 1 || mode == 2));
#if HAS_VENDOR
        if (mode == 3)
            assert(n.oob_ops == &sunxi_nfc_allwinner_oob_ops);
        else
#endif
            assert(n.oob_ops == &sunxi_nfc_plain_oob_ops);
#endif
        for (unsigned ecc_seed = 0; ecc_seed < 2; ecc_seed++) {
            const u32 initial = 0x12340000 | (1U << 1);
            writel(initial, nfc.regs + NFC_REG_ECC_CTL);
            sunxi_nfc_randomizer_config(&n, page, ecc_seed);
            u32 configured = initial;
            if (mode != 0)
                configured = (initial & ~NFC_RANDOM_SEED_MSK) |
                    NFC_RANDOM_SEED(sunxi_nfc_randomizer_state(&n, page, ecc_seed));
            assert(readl(nfc.regs + NFC_REG_ECC_CTL) == configured);
            sunxi_nfc_randomizer_enable(&n);
            assert(readl(nfc.regs + NFC_REG_ECC_CTL) ==
                   (configured | (mode ? NFC_RANDOM_EN(&nfc) : 0)));
            sunxi_nfc_randomizer_disable(&n);
            assert(readl(nfc.regs + NFC_REG_ECC_CTL) == configured);
        }
        sunxi_nfc_randomizer_enable(&n);
        assert(!!(readl(nfc.regs + NFC_REG_ECC_CTL) & NFC_RANDOM_EN(&nfc)) == (mode != 0));

        u8 marker[4] = { bad_marker ? 0 : 0xff, bad_marker ? 0 : 0xff, 0x12, 0x34 };
        u8 expected[4], actual[4], decoded[4];
        memcpy(expected, marker, 4);
        /* Oracle for the ordinary format: a plain on-flash marker needs
         * software compensation whenever hardware scrambling is enabled.
         * The vendor format intentionally has no such compensation. */
        if (mode == 1 || mode == 2) {
            u16 state = sunxi_nfc_randomizer_state(&n, page, true);
            expected[0] ^= state;
            expected[1] ^= sunxi_nfc_randomizer_step(state, 8);
        }
        writel(sunxi_nfc_buf_to_user_data(expected), nfc.regs + caps.reg_user_data);
        sunxi_nfc_hw_ecc_get_prot_oob_bytes(&n, decoded, 0, true, page, 4);
        bool read_bad = memcmp(decoded, marker, 4) != 0;
#if !HAS_VENDOR
        sunxi_nfc_hw_ecc_set_prot_oob_bytes(&n, marker, 0, true, page);
#else
        sunxi_nfc_hw_ecc_set_prot_oob_bytes(&n, marker, 0, true, page, 4);
#endif
        sunxi_nfc_user_data_to_buf(readl(nfc.regs + caps.reg_user_data), actual);
        bool write_bad = memcmp(actual, expected, 4) != 0;
        if (read_bad && !bad_reads)
            printf("first mismatch: hw=%u mode=%u page=%u marker=%02x%02x decoded=%02x%02x expected_tx=%02x%02x actual_tx=%02x%02x\n",
                   hw, mode, page, marker[0], marker[1], decoded[0], decoded[1],
                   expected[0], expected[1], actual[0], actual[1]);
        assert(read_bad == (!EXPECT_FIXED && mode == 2));
        assert(write_bad == (!EXPECT_FIXED && mode == 2));
        bad_reads += read_bad;
        bad_writes += write_bad;
        cases++;
    }
    printf("cases=%u BBM_read_mismatches=%u BBM_write_mismatches=%u (expected late-init policy)\n",
           cases, bad_reads, bad_writes);
    puts("ALL BBM INIT CHECKS PASSED");
    return 0;
}
