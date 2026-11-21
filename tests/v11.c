/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independent physical media, corrected USER_DATA and ECC flags. No flash writes. */
static u8 image[16384]; /* Includes the spare area of an 8 KiB test page. */
static struct caps caps = {
    .reg_user_data = 0x100, .reg_user_data_len = 0x70, .reg_pat_id = 0x80,
    .reg_ecc_err_cnt = 0x90, .ecc_err_mask = 0xffff, .max_ecc_steps = 32,
    .reg_pat_found = 0x84, .pat_found_mask = 0xffff,
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

static unsigned observed_steps, tail_reads;

/* Supply SRAM and the slot-zero correction count when a PIO step is fetched. */
static void observe_pio(struct nand_chip *n, unsigned col, void *dst, unsigned len, int ret)
{
    unsigned size = n->ecc.size;
    if (ret) return;
    if (!dst && len == size && col < n->mtd.writesize) {
        unsigned step = col / size;
        assert(col % size == 0 && !(observed_steps & BIT(step)));
        observed_steps |= BIT(step);
        memcpy(controller.regs + NFC_RAM0_BASE, image + col, size);
        writel(step + 1, controller.regs + caps.reg_ecc_err_cnt);
    }
    if (dst == oob_data + n->mtd.oobsize - 8 && len == 8)
        tail_reads++;
}

static void begin_pio(unsigned fail)
{
    memset(&flow, 0, sizeof(flow));
    flow.enabled = true;
    flow.fail_at = fail;
    observed_steps = tail_reads = 0;
    oob_transfer_observer = observe_pio;
}

/* The expected page-image slice is independent of the driver's rounding.
 * Inject a failure at each transport event in a fresh read. */
static unsigned pio_ranges(void)
{
    unsigned checked = 0;
    for (unsigned size = 512; size <= 1024; size *= 2)
    for (unsigned first = 0; first < 4; first++)
    for (unsigned end = first + 1; end <= 4; end++)
    for (unsigned mode = 0; mode < (first == 0 && end == 4 ? 4U : 2U); mode++) {
        bool full = mode >= 2, with_oob = mode == 3;
        unsigned trim = mode == 1 ? 3 : 0;
        unsigned mask = BIT(end) - BIT(first), boundaries = 0;
        for (unsigned fail = 0; fail <= boundaries; fail++) {
            struct nand_chip n = setup(size, 4);
            for (unsigned i = 0; i < n.mtd.writesize; i++)
                image[i] = 0x10 + i / size;
            memset(main_data, 0xbe, n.mtd.writesize);
            memset(image + n.mtd.writesize + n.mtd.oobsize - 8, 0x66, 8);
            begin_pio(fail);
            int ret = full ? sunxi_nfc_hw_ecc_read_page(&n, main_data, with_oob, 0) :
                sunxi_nfc_hw_ecc_read_subpage(&n, first * size + trim,
                    (end - first) * size - 2 * trim, main_data, 0);
            assert(!flow.ecc && !flow.random);
            if (fail) {
                assert(ret == -ETIMEDOUT && flow.calls == fail);
            } else {
                boundaries = flow.calls;
                assert(observed_steps == mask);
                assert(flow.issued == end - first && ret == (int)end);
                unsigned corrected = 0;
                for (unsigned i = first; i < end; i++) corrected += i + 1;
                assert(n.mtd.ecc_stats.corrected == corrected && !n.mtd.ecc_stats.failed);
                for (unsigned i = 0; i < n.mtd.writesize; i++)
                    assert(main_data[i] == (mask & BIT(i / size) ? image[i] : 0xbe));
                assert(tail_reads == with_oob);
                for (unsigned i = n.mtd.oobsize - 8; i < n.mtd.oobsize; i++)
                    assert(oob_data[i] == (with_oob ? 0x66 : 0xbe));
            }
            checked++;
        }
    }
    memset(&flow, 0, sizeof(flow));
    return checked;
}

static unsigned erased_step_ranges(void)
{
    static const u8 layouts[][4] = { {4, 4, 4, 4}, {8, 8, 4, 0} };
    unsigned checked = 0;
    for (unsigned size = 512; size <= 1024; size *= 2)
    for (unsigned layout = 0; layout < ARRAY_SIZE(layouts); layout++)
    for (unsigned step = 0; step < 4; step++)
    for (unsigned fail = 0; fail <= 2; fail++) {
        struct nand_chip n = setup(size, 4);
        struct sunxi_nfc_ecc_read r = { .buf = main_data, .dma = true };
        struct sunxi_nfc_ecc_snapshot st = status(ERROR); /* hardware slot zero */
        const u8 *user = layouts[layout];
        unsigned off = 0, len = n.ecc.bytes + user[step], data = step * size;
        for (unsigned s = 0; s < step; s++) off += n.ecc.bytes + user[s];
        unsigned col = n.mtd.writesize + off;
        n.options = NAND_NEED_SCRAMBLING;
        if (layout) { n.user_data_step = 8; n.user_data_total = 20; }
        memset(main_data, 0xbe, n.mtd.writesize);
        image[data + size - 1] = 0xfe;
        image[col + len - 1] = 0xfc;
        image[col] = step ? 0xfe : 0;
        if (!step) image[col + 1] = 0x35;
        fail_raw_at = fail;
        int ret = sunxi_nfc_oob_read_batch(&n, &r, &st, step, 1);
        if (fail) {
            assert(ret == -ETIMEDOUT && !r.has_erased_chunk && !n.mtd.ecc_stats.corrected);
        } else {
            assert(!ret && r.has_erased_chunk && r.max_bitflips == 3U + !!step);
            assert(n.mtd.ecc_stats.corrected == r.max_bitflips && !n.mtd.ecc_stats.failed);
            for (unsigned i = 0; i < n.mtd.writesize; i++)
                assert(main_data[i] == (i >= data && i < data + size ? 0xff : 0xbe));
            for (unsigned i = 0; i < n.mtd.oobsize; i++) {
                u8 expected = i < off || i >= off + len ? 0xbe : 0xff;
                if (!step && i < 2) expected = image[col + i];
                assert(oob_data[i] == expected);
            }
        }
        assert(raw_calls == (fail ? fail : 2));
        checked++;
    }
    return checked;
}

/* A later raw reread must not forget an earlier erased step. A media
 * failure updates ecc_stats; only a failed raw command fails this batch.
 */
static unsigned mixed_erased_steps(void)
{
    static const unsigned first_noise[] = {0, 1, 17}, second_noise[] = {0, 2, 17};
    unsigned checked = 0;
    for (unsigned first = 0; first < ARRAY_SIZE(first_noise); first++)
    for (unsigned second = 0; second < ARRAY_SIZE(second_noise); second++)
    for (unsigned split = 0; split < 2; split++)
    for (unsigned fail = 0; fail <= 4; fail++) {
        struct nand_chip n = setup(512, 2);
        struct sunxi_nfc_ecc_read r = { .buf = main_data, .dma = true, .oob_required = true };
        const unsigned noise[] = {first_noise[first], second_noise[second]};
        n.options = NAND_NEED_SCRAMBLING;
        for (unsigned step = 0; step < 2; step++)
            set_noise(step * 512, noise[step]);
        fail_raw_at = fail;
        int ret = 0;
        for (unsigned start = 0; start < 2 && !ret; start += split ? 1 : 2) {
            struct sunxi_nfc_ecc_snapshot s = { .status = split ? 1 : 3, .count_reg = ~0U };
            ret = sunxi_nfc_oob_read_batch(&n, &r, &s, start, split ? 1 : 2);
        }
        unsigned finished = fail ? (fail - 1) / 2 : 2;
        unsigned corrected = 0, failed = 0, maximum = 0;
        bool erased = false;
        for (unsigned step = 0; step < finished; step++) {
            if (noise[step] <= n.ecc.strength) {
                corrected += noise[step];
                if (noise[step] > maximum) maximum = noise[step];
                erased = true;
            } else {
                failed++;
            }
        }
        assert(ret == (fail ? -ETIMEDOUT : 0));
        assert(r.has_erased_chunk == erased);
        assert(r.max_bitflips == maximum && n.mtd.ecc_stats.corrected == corrected);
        assert(n.mtd.ecc_stats.failed == failed && raw_calls == (fail ? fail : 4));
        if (!ret) {
            unsigned plain = plain_read_calls, randomized = random_read_calls;
            assert(sunxi_nfc_oob_finish_read(&n, &r, NULL) == (int)maximum);
            assert(plain_read_calls - plain == erased && random_read_calls - randomized == !erased);
        }
        checked++;
    }
    return checked;
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
    /* A deferred-format subpage delegates to the full-page read even without
     * an OOB request: the firmware classifier needs the other steps. */
    struct nand_chip full = setup(1024, 2);
    full.allwinner_oob = true;
    full.options = NAND_NEED_SCRAMBLING;
    full.ecc.read_page = sunxi_nfc_hw_ecc_read_page;
    begin_pio(0);
    assert(sunxi_nfc_allwinner_oob_read_subpage(&full, 1030, 15, main_data, 0) == 2);
    assert(observed_steps == 3 && !flow.ecc && !flow.random);
    memset(&flow, 0, sizeof(flow));
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

/* Independent controller register maps and 8-KiB media layouts shared by
 * the full read/write callback tests. Return the unprotected tail offset. */
static unsigned transfer_geometry(struct nand_chip *n, struct caps *c, unsigned backend,
                                  unsigned profile, unsigned lengths[8])
{
    static const unsigned wide[] = {32, 32, 32, 32, 32, 16, 0, 0};
    bool variable = backend == 2, fw = profile == 2;
    c->reg_user_data_len = variable ? 0x70 : 0;
    c->reg_ecc_err_cnt = variable ? 0x50 : 0x40;
    c->reg_user_data = variable ? 0x80 : 0x50;
    c->reg_pat_found = variable ? 0x3c : NFC_REG_ECC_ST;
    c->reg_pat_id = variable ? 0x118 : 0xa4;
    c->ecc_err_mask = variable ? ~0U : 0xffffU;
    c->pat_found_mask = variable ? ~0U : 0xffff0000U;
    c->reg_spare_area = variable ? 0x114 : 0xa0;
    c->max_ecc_steps = variable ? 32 : 16;
    c->max_user_data_len = variable ? 32 : 0;
    c->sram_size = variable ? 8192 : 1024;
    controller.caps = c;
    controller.use_mdma = backend != 0;
    n->options = profile ? NAND_NEED_SCRAMBLING : 0;
    n->allwinner_oob = fw;
    n->user_data_step = variable ? (fw ? 16 : 32) : 0;
    n->user_data_total = variable ? (fw ? 16 : 176) : 0;
    n->ecc.bytes = 56;
    n->ecc.strength = 32;
    for (unsigned i = 0; i < 8; i++)
        lengths[i] = !variable ? 4 : fw ? (i ? 0 : 16) : wide[i];
#if HAS_FW
    if (fw) c->spare_is_erased = variable ? sun50i_h6_nfc_spare_is_erased : sun4i_a10_nfc_spare_is_erased;
#endif
    return 8 * 56 + (variable ? n->user_data_total : 8 * 4);
}

#include "v11_program.c"
#include "v11_dma_read.c"

int main(void)
{
    unsigned mixed = mixed_erased_steps();
    unsigned checked = ordinary_cases();
    unsigned pio = pio_ranges();
    unsigned erased = erased_step_ranges();
    column_cases(); length_cases();
#if HAS_FW
    firmware_cases();
#endif
    printf("%u ordinary cases; %u PIO range/fault, %u erased-step geometry, %u mixed raw trials; "
           "core bad-block checks, DMA retry and packed register checks passed; firmware cases: %u\n",
           checked, pio, erased, mixed, HAS_FW ? 25 : 0);
    program_cases();
    dma_read_cases();
    return 0;
}
