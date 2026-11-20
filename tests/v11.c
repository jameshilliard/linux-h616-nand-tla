/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independent physical media, corrected USER_DATA and ECC flags. No flash writes. */
static u8 image[4096];
static struct caps caps = {
    .reg_user_data = 0x100, .reg_user_data_len = 0x70, .reg_pat_id = 0x80,
    .reg_ecc_err_cnt = 0x90, .ecc_err_mask = 0xffff, .max_ecc_steps = 32,
    .max_user_data_len = 32, .sram_size = 1024,
};
static unsigned marker_commands;
static enum status_kind { NORMAL, ERROR, ONES, ZEROES, ERROR_ONES } kind;
static bool want, dma;
static unsigned marker, companion, noise;

static void observe(struct nand_chip *n, unsigned col, void *dst, unsigned len, int ret)
{
    if (col == n->mtd.writesize && len == 2 && dst == n->oob_poi)
        marker_commands++;
}

static struct nand_chip setup(unsigned size, unsigned steps)
{
    unsigned page = size * steps;
    struct nand_chip n = { .controller = &controller, .oob_poi = oob_data,
        .ecc = { .size = size, .steps = steps, .bytes = 28, .strength = 16 },
        .mtd = { .writesize = page, .erasesize = 64 * page, .oobsize = 32 * steps + 8,
                 .ecc_step_size = size }, .page_shift = __builtin_ctz(page),
        .pagemask = 65535, .badblockbits = 8 };
    assert(page + n.mtd.oobsize <= sizeof(image));
    controller.caps = &caps;
    controller.recovery_failed = false;
    format_image = image; format_image_len = page + n.mtd.oobsize;
    memset(image, 0xff, sizeof(image)); memset(main_data, 0xff, page);
    memset(oob_data, 0xbe, n.mtd.oobsize);
    memset(controller.regs, 0, sizeof(controller.regs));
    memset(controller.regs + NFC_RAM0_BASE, 0xff, size);
    raw_calls = marker_commands = fail_raw_at = column = 0;
    oob_transfer_observer = observe;
    return n;
}

static void register_marker(struct nand_chip *n, unsigned value, unsigned second)
{
    u8 decoded[] = { value, second, 0xff, 0xff };
    if (n->options & NAND_NEED_SCRAMBLING)
        sunxi_nfc_randomize_bbm(n, 0, decoded);
    writel((u32)decoded[0] | ((u32)decoded[1] << 8) | 0xffff0000U,
           controller.regs + caps.reg_user_data);
}

static void set_noise(unsigned col, unsigned bits)
{
    for (unsigned i = 0; i < bits; i++)
        image[col + i / 8] &= ~(1U << (i % 8));
}

static struct sunxi_nfc_ecc_snapshot status(enum status_kind type)
{
    bool error = type == ERROR || type == ERROR_ONES;
    bool pattern = type == ONES || type == ZEROES || type == ERROR_ONES;
    writel(type == ONES || type == ERROR_ONES, controller.regs + caps.reg_pat_id);
    return (struct sunxi_nfc_ecc_snapshot){ .status = error, .pattern_found = pattern,
                                            .count_reg = ~0U };
}

/* This callback is also called by the real NAND core's nand_block_bad(). */
static int single(struct nand_chip *n, int page)
{
    struct sunxi_nfc_ecc_read read = { .buf = main_data, .oob_required = want, .dma = dma, .page = page };
    struct sunxi_nfc_ecc_snapshot st = status(kind);
    int ret = sunxi_nfc_oob_read_batch(n, &read, &st, 0, 1);
    return ret < 0 ? ret : sunxi_nfc_oob_finish_read(n, &read, NULL);
}

static unsigned ordinary_cases(void)
{
    unsigned cases = 0;
    const unsigned errors[] = {0, 1, 16, 17};
    const unsigned marks[] = {0xff, 0, 0xf7};
    for (unsigned size = 512; size <= 1024; size *= 2)
    for (unsigned d = 0; d < 2; d++)
    for (unsigned s = 0; s < 2; s++)
    for (unsigned w = 0; w < 2; w++)
    for (kind = NORMAL; kind <= ERROR_ONES; kind++)
    for (unsigned p = 0; p < ARRAY_SIZE(marks); p++)
    for (unsigned c = 0; c < 2; c++)
    for (unsigned z = 0; z < ARRAY_SIZE(errors); z++) {
        struct nand_chip n = setup(size, 1);
        bool err = kind == ERROR || kind == ERROR_ONES;
        bool pattern = kind == ONES || kind == ZEROES;
        dma = d; want = w; marker = marks[p]; companion = c ? 0x35 : 0xff; noise = errors[z];
        n.options = s ? NAND_NEED_SCRAMBLING : 0;
        n.ecc.read_oob = single;
        image[size] = marker; image[size + 1] = companion;
        set_noise(size + 4, noise);
        /* Deliberately disagree with the raw word on successful ECC. */
        register_marker(&n, 0xfc, 0x69);
        writel(3, controller.regs + caps.reg_ecc_err_cnt);
        int ret = single(&n, 0);
        unsigned corrected = kind == NORMAL ? 3 : err && noise <= 16 ? noise : 0;
        assert(ret == (int)corrected && n.mtd.ecc_stats.corrected == corrected);
        assert(n.mtd.ecc_stats.failed == (err && noise > 16));
        assert(marker_commands == (want && pattern));
        if (want) {
            unsigned expected = kind == NORMAL ? 0xfc : marker;
            assert(oob_data[0] == expected && oob_data[1] == (kind == NORMAL ? 0x69 : companion));
            assert(nand_block_bad(&n, 0) == (expected != 0xff));
        }
        cases++;
    }
    return cases;
}

static void column_cases(void)
{
    /* Only step zero owns the BBM. Step one's first two bytes count as
     * ordinary noise; a later pattern must not reread step zero's marker. */
    for (unsigned d = 0; d < 2; d++)
    for (unsigned p = 0; p < 2; p++) {
        struct nand_chip n = setup(1024, 2);
        struct sunxi_nfc_ecc_read r = { .buf = main_data, .oob_required = true, .dma = d };
        struct sunxi_nfc_ecc_snapshot st = status(NORMAL);
        register_marker(&n, 0xe7, 0xab);
        assert(!sunxi_nfc_oob_read_batch(&n, &r, &st, 0, 1));
        st = status(p ? ZEROES : ERROR);
        image[2048 + 32] = 0;
        assert(!sunxi_nfc_oob_read_batch(&n, &r, &st, 1, 1));
        assert(sunxi_nfc_oob_finish_read(&n, &r, NULL) == (p ? 0 : 8));
        assert(oob_data[0] == 0xe7 && oob_data[1] == 0xab && !marker_commands);
    }
    /* The extra tail spans column selection and transport, both before
     * the marker. A failed marker must reach the bad-block checker. */
    for (unsigned f = 1; f <= 3; f++) {
        struct nand_chip n = setup(1024, 1);
        n.ecc.read_oob = single; kind = ZEROES; dma = true; want = true;
        fail_raw_at = f;
        assert(nand_block_bad(&n, 0) == -ETIMEDOUT);
        assert(marker_commands == (f == 3));
    }
}

static void wrapper_cases(void)
{
    for (unsigned sub = 0; sub < 2; sub++)
    for (unsigned first_fails = 0; first_fails < 2; first_fails++)
    for (unsigned poisoned = 0; poisoned < 2; poisoned++)
    for (unsigned ri = 0; ri < 3; ri++) {
        struct nand_chip n = setup(1024, 2);
        controller.recovery_failed = poisoned;
        dma_result = (int[]){0, 3, -ENXIO}[ri]; dma_calls = pio_calls = 0;
        fail_raw_at = first_fails;
        int ret = sub ? sunxi_nfc_hw_ecc_read_subpage_dma(&n, 1030, 15, main_data, 0) :
                        sunxi_nfc_hw_ecc_read_page_dma(&n, main_data, false, 0);
        bool retry = !first_fails && dma_result < 0 && !poisoned;
        assert(ret == (first_fails ? -ETIMEDOUT : retry ? (sub ? 8 : 7) : dma_result));
        assert(dma_calls == !first_fails && pio_calls == retry);
        if (!first_fails) assert(dma_first == sub && dma_end == 2);
    }
}

static void length_cases(void)
{
    /* Prior transfer left arbitrary bits in all four physical words. */
    for (unsigned len = 0; len <= 32; len += 4) {
        struct nand_chip n = setup(1024, 2);
        unsigned reg = caps.reg_user_data_len;
        for (unsigned i = 0; i < 4; i++) writel(~0U, controller.regs + reg + 4*i);
        memset(mmio_writes, 0, sizeof(mmio_writes));
        sunxi_nfc_reset_user_data_len(&controller);
        assert(!mmio_writes[reg / 4] && readl(controller.regs + reg) == ~0U);
        sunxi_nfc_set_user_data_len_pio(&controller, len);
        for (unsigned i = 0; i < 4; i++) {
            assert(mmio_writes[reg / 4 + i] == 1);
            assert(readl(controller.regs + reg + 4*i) == (i ? 0U : len / 4));
        }
        sunxi_nfc_set_user_data_len_dma(&n, 0, 2);
        assert(readl(controller.regs + reg) == 0x11);
        sunxi_nfc_set_user_data_len_pio(&controller, 4);
        assert(readl(controller.regs + reg) == 1);
    }
}

#if HAS_FW
static void firmware_cases(void)
{
    /* The same physical zero marker that an ordinary pattern read restores
     * must not override the deferred format's page-wide classification. */
    for (unsigned v = 0; v < 2; v++)
    for (unsigned d = 0; d < 2; d++)
    for (unsigned w = 0; w < 2; w++)
    for (unsigned i = 0; i < 3; i++) {
        struct nand_chip n = setup(1024, 1);
        struct sunxi_nfc_ecc_read r = { .buf = main_data, .dma = d, .oob_required = w };
        struct sunxi_nfc_ecc_snapshot st = status(i == 2 ? ZEROES : ERROR_ONES);
        u8 decoded[4] = { i == 1 ? 0 : 0xff, 0xff, 0xff, 0xff };
        n.allwinner_oob = true; n.options = NAND_NEED_SCRAMBLING;
        caps.spare_is_erased = v ? sun50i_h6_nfc_spare_is_erased : sun4i_a10_nfc_spare_is_erased;
        sunxi_nfc_randomize_buf(sunxi_nfc_randomizer_state(&n, 0, true), decoded, sizeof(decoded));
        memcpy(controller.regs + caps.reg_user_data, decoded, sizeof(decoded));
        image[1024] = 0;
        assert(!sunxi_nfc_oob_read_batch(&n, &r, &st, 0, 1));
        assert(!sunxi_nfc_oob_finish_read(&n, &r, NULL) && !marker_commands);
        assert(n.mtd.ecc_stats.failed == (i != 0));
        assert(oob_data[0] == (i == 0 ? 0xff : i == 2 ? 0 : decoded[0]));
    }
}
#endif

int main(void)
{
    unsigned checked = ordinary_cases();
    column_cases(); wrapper_cases(); length_cases();
#if HAS_FW
    firmware_cases();
#endif
    printf("%u ordinary cases; core bad-block checks, later steps, column faults, "
           "DMA retry and packed register checks passed; firmware cases: %u\n", checked, HAS_FW ? 24 : 0);
    return 0;
}
