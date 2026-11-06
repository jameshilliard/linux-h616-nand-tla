/* SPDX-License-Identifier: GPL-2.0-or-later */
static void bytes(FILE *f, const u8 *b)
{
    fprintf(f, "[%u,%u,%u,%u]", b[0], b[1], b[2], b[3]);
}

int main(void)
{
    FILE *f = fopen("randomizer-policy.jsonl", "w");
    assert(f);
    struct caps caps[2] = {
        { .reg_user_data = 0x50, .random_en = 1U << 9,
          .plain_oob_ops = TEST_A10_PLAIN_OPS, .scrambled_oob_ops = TEST_A10_SCRAMBLED_OPS,
          .allwinner_oob_ops = TEST_A10_VENDOR_OPS },
        { .reg_user_data = 0x80, .reg_user_data_len = 0x70, .random_en = 1U << 5,
          .plain_oob_ops = TEST_H6_PLAIN_OPS, .scrambled_oob_ops = TEST_H6_SCRAMBLED_OPS,
          .allwinner_oob_ops = TEST_H6_VENDOR_OPS },
    };
    /* Preserve each controller's register state across differently configured chips. */
    struct sunxi_nfc controllers[2] = { { .caps = &caps[0] }, { .caps = &caps[1] } };
    for (unsigned i = 0; i < 2; i++)
        writel(0x92340002, controllers[i].regs + NFC_REG_ECC_CTL);
    for (unsigned i = 0; i < ARRAY_SIZE(inputs); i++) {
        const struct randomizer_input *t = &inputs[i];
        struct sunxi_nfc *nfc = &controllers[t->hw];
        struct nand_chip n = {
            .controller = nfc,
            .mtd = { .writesize = 16384, .erasesize = 16384 * t->ppb,
                     .ecc_step_size = t->step },
            .base = { .step_size = t->step, .strength = 40 },
            .options = t->mode == 1 ? NAND_NEED_SCRAMBLING : 0,
        };
        vendor_property = t->mode == 3;
        assert(!sunxi_nand_attach_chip(&n));
        if (t->mode == 2)
            assert(!tc58teg5dclta00_init(&n));
#if SINGLE_INSTALL
        assert(!n.oob_ops);
        n.oob_ops = sunxi_nfc_get_oob_ops(&n);
#elif DEFERRED_SELECTION
        sunxi_nfc_select_oob_ops(&n);
#endif
#if HAS_OPS_POLICY
        const struct sunxi_nfc_oob_ops *ops = sunxi_nfc_get_oob_ops(&n);
#else
        const struct sunxi_nfc_oob_ops *ops = n.oob_ops;
#endif
#if HAS_RANDOMIZED_FIELD
        bool randomized = ops->TEST_RANDOMIZER_MEMBER;
#else
        bool randomized = n.options & NAND_NEED_SCRAMBLING;
#endif
        fprintf(f, "{\"id\":%u,\"randomized\":%s,\"compensate\":%s,"
                   "\"pageSeed\":%u,\"eccSeed\":%u,\"regs\":[", i,
                randomized ? "true" : "false", ops->transform_bbm ? "true" : "false",
                sunxi_nfc_randomizer_state(&n, t->page, false),
                sunxi_nfc_randomizer_state(&n, t->page, true));
        for (unsigned ecc = 0; ecc < 2; ecc++) {
            u32 before = readl(nfc->regs + NFC_REG_ECC_CTL);
            sunxi_nfc_randomizer_config(&n, t->page, ecc);
            u32 configured = readl(nfc->regs + NFC_REG_ECC_CTL);
            sunxi_nfc_randomizer_enable(&n);
            u32 on = readl(nfc->regs + NFC_REG_ECC_CTL);
            sunxi_nfc_randomizer_disable(&n);
            fprintf(f, "%s{\"before\":%u,\"configured\":%u,\"on\":%u,\"off\":%u}",
                    ecc ? "," : "", before, configured, on,
                    readl(nfc->regs + NFC_REG_ECC_CTL));
        }
        u8 rx[4], tx[4], scratch[4] = {};
        writel(sunxi_nfc_buf_to_user_data(t->decoded), nfc->regs + caps[t->hw].reg_user_data);
        sunxi_nfc_hw_ecc_get_prot_oob_bytes(&n, rx, 0, t->bbm, t->page, 4);
        sunxi_nfc_hw_ecc_set_prot_oob_bytes(&n, t->marker, 0, t->bbm, t->page, 4);
        sunxi_nfc_user_data_to_buf(readl(nfc->regs + caps[t->hw].reg_user_data), tx);
        fprintf(f, "],\"rx\":"); bytes(f, rx);
        fprintf(f, ",\"tx\":"); bytes(f, tx);
        transfer_result = t->fail;
        for (unsigned write = 0; write < 2; write++) {
            u32 before = readl(nfc->regs + NFC_REG_ECC_CTL);
            int ret = write ? sunxi_nfc_randomizer_write_buf(&n, scratch, 4, true, t->page)
                            : sunxi_nfc_randomizer_read_buf(&n, scratch, 4, false, t->page);
            fprintf(f, ",\"%s\":{\"before\":%u,\"during\":%u,\"after\":%u,"
                       "\"ret\":%d,\"ecc\":%s}", write ? "write" : "read", before,
                    transfer_control, readl(nfc->regs + NFC_REG_ECC_CTL), ret,
                    write ? "true" : "false");
        }
        fprintf(f, "}\n");
    }
    assert(!fclose(f));
    f = fopen("randomizer-tables.jsonl", "w");
    assert(f);
    for (unsigned step = 512; step <= 1024; step *= 2)
    for (unsigned i = 0; i < 128; i++) {
        u16 state = step == 512 ? sunxi_nfc_randomizer_ecc512_seeds[i]
                               : sunxi_nfc_randomizer_ecc1024_seeds[i];
        u8 mask[4] = {};
        sunxi_nfc_randomize_buf(state, mask, 4);
        fprintf(f, "{\"step\":%u,\"index\":%u,\"pageSeed\":%u,\"eccSeed\":%u,\"bytes\":",
                step, i, sunxi_nfc_randomizer_page_seeds[i], state);
        bytes(f, mask);
        fprintf(f, "}\n");
    }
    assert(!fclose(f));
    printf("%zu policy traces, 256 ECC-table/byte-stream observations\n", ARRAY_SIZE(inputs));
    puts("ALL RANDOMIZER TRACE CHECKS PASSED");
    return 0;
}
