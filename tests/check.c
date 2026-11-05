/* SPDX-License-Identifier: GPL-2.0-or-later */
static unsigned tests;
static void control_flow(void);
static void attach_geometry(void);
static void snapshot_cache(void);
static void bitmap_boundaries(void);
static void attach_policy(void);
static void metadata_identity(void);
static void format_roundtrip(void);
static void compatibility(void);
static void codec_bridge(void);
static void oob_pipeline(void);

static struct caps caps(bool variable)
{
    return (struct caps) {
        .sram_size = variable ? 8192 : 1024,
        .reg_user_data_len = variable ? 0x70 : 0,
        .reg_user_data = variable ? 0x80 : 0x50,
        .reg_pat_id = variable ? 0x118 : 0xa4,
        .reg_pat_found = variable ? 0x3c : 0x38,
        .reg_ecc_err_cnt = variable ? 0x50 : 0x40,
        .reg_spare_area = variable ? 0x114 : 0xa0,
        .ecc_err_mask = variable ? UINT32_MAX : 0xffff,
        .pat_found_mask = variable ? UINT32_MAX : 0xffff0000,
        .max_ecc_steps = variable ? 32 : 16,
        .user_data_len_tab = sunxi_user_data_len_h6,
        .nuser_data_tab = ARRAY_SIZE(sunxi_user_data_len_h6),
        .legacy_max_strength = !variable,
        .spare_is_erased = variable ? sun50i_h6_nfc_spare_is_erased : sun4i_a10_nfc_spare_is_erased,
    };
}

static struct nand_chip chip(unsigned steps, unsigned bytes, unsigned spare)
{
    return (struct nand_chip) {
        .ecc = {.size = 1024, .bytes = bytes, .steps = steps, .strength = 32},
        .mtd = {.writesize = steps * 1024, .oobsize = spare,
                .erasesize = steps * 1024 * 64, .ecc_step_size = 1024},
        .controller = &controller, .oob_poi = oob_data,
    };
}

static void geometry(void)
{
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned vendor = 0; vendor < 2; vendor++) {
        struct caps c = caps(variable);
        controller.caps = &c;
        for (unsigned n = 1; n <= c.max_ecc_steps; n++)
        for (unsigned parity = 28; parity <= 140; parity += 2)
        for (unsigned spare = 4; spare <= n * 32 + 20; spare += 4) {
            struct nand_chip nand = chip(n, parity, n * parity + spare);
            int ret = 0;
            if (variable)
                ret = vendor ? sunxi_nfc_allwinner_oob_init_user_data(&nand, n) :
                    sunxi_nfc_maximize_user_data(&nand, nand.mtd.oobsize, parity, n);
            assert(!ret);
            unsigned sum = 0;
            for (unsigned i = 0; i < n; i++) sum += sunxi_nfc_user_data_sz(&nand, i);
            /* Final init rejects geometries that cannot hold the chosen layout. */
            if (sum > spare) { free(nand.user_data_bytes); continue; }
            assert(sunxi_nfc_user_data_sz(&nand, 0) >= 4);
            unsigned off = 0, free_bytes = 0;
            for (unsigned i = 0; i < n; i++) {
                unsigned len = sunxi_nfc_user_data_sz(&nand, i);
                struct mtd_oob_region region;
                assert(len <= 32 && !(len % 4));
                assert(sunxi_get_oob_offset(&nand, &nand.ecc, i) == (int)off);
                assert(sunxi_get_ecc_offset(&nand, &nand.ecc, i) == (int)(off + len));
                assert(!sunxi_nand_ooblayout_ecc(&nand.mtd, i, &region));
                assert(region.offset == off + len && region.length == parity);
                assert(!sunxi_nand_ooblayout_free(&nand.mtd, i, &region));
                assert(region.offset == off + (i ? 0 : 2));
                assert(region.length == len - (i ? 0 : 2));
                assert(region.offset >= 2 && region.offset + region.length <= off + len);
                free_bytes += region.length;
                off += len + parity;
            }
            assert(off <= nand.mtd.oobsize && free_bytes == sum - 2);
            assert(sunxi_get_oob_offset(&nand, &nand.ecc, n) == (int)off);
            struct mtd_oob_region r;
            assert(sunxi_nand_ooblayout_ecc(&nand.mtd, n, &r) == -ERANGE);
            assert(sunxi_nand_ooblayout_free(&nand.mtd, n, &r) == -ERANGE);
            for (unsigned first = 0; first < n;) {
                int batch = sunxi_nfc_dma_batch_steps(&nand, first, n);
                assert(batch > 0 && first + batch <= n);
                if (first && variable) assert(batch == 1);
                unsigned words = 0;
                for (unsigned i = first; i < first + batch; i++)
                    words += sunxi_nfc_user_data_sz(&nand, i) / 4;
                assert(words <= (variable ? 32 : 16));
                if (variable) {
                    memset(controller.regs + 0x70, 0xff, 16);
                    sunxi_nfc_set_user_data_len_dma(&nand, first, batch);
                    for (unsigned i = 0; i < 32; i++) {
                        u32 reg = readl(controller.regs + 0x70 + i / 8 * 4);
                        unsigned code = (reg >> (i % 8 * 4)) & 15;
                        unsigned expected = i < (unsigned)batch ?
                            sunxi_nfc_user_data_sz(&nand, first + i) / 4 : 0;
                        assert(code == expected);
                    }
                }
                first += batch;
            }
            if (variable && vendor) assert(sum == min(n * 4, 16));
            free(nand.user_data_bytes);
            tests++;
        }
        if (variable) {
            sunxi_nfc_reset_user_data_len(&controller);
            for (unsigned i = 0; i < 32; i++)
                assert(!readl(controller.regs + 0x70 + i / 8 * 4));
            for (unsigned len = 0; len <= 32; len += 4) {
                sunxi_nfc_set_user_data_len_pio(&controller, len);
                assert(readl(controller.regs + 0x70) == len / 4);
            }
        }
    }
    printf("geometry/layout/batch/register checks: %u\n", tests);
}

static void check_length_image(const u8 *user,unsigned first,unsigned count)
{
    for(unsigned slot=0;slot<32;slot++) {
        u32 word=readl(controller.regs+0x70+(slot/8)*4);
        unsigned expected=slot<count ? user[first+slot]/4 : 0;
        assert(((word>>((slot%8)*4))&15)==expected);
    }
    assert(readl(controller.regs+0x6c)==0x76543210);
    assert(readl(controller.regs+0x80)==0x89abcdef);
}

static void length_sequence_checks(void)
{
    static const unsigned starts[]={0,1,7,8,15,16,31};
    static const unsigned counts[]={1,7,8,9,16,17,31,32};
    u8 wide[32], mixed[32];
    unsigned runs=0;
    struct caps c=caps(true);
    struct nand_chip nand=chip(32,56,32*60);
    memset(wide,4,sizeof(wide));
    controller.caps=&c;
    memset(controller.regs,0xff,sizeof(controller.regs));
    writel(0x76543210,controller.regs+0x6c);
    writel(0x89abcdef,controller.regs+0x80);
    for(unsigned first=0;first<ARRAY_SIZE(starts);first++)
    for(unsigned len=0;len<=32;len+=4) {
        /* Preserve registers across all requests, including 32->1 slots,
         * PIO/DMA changes, zero-length metadata and nonzero logical steps. */
        nand.user_data_bytes=wide;
        sunxi_nfc_set_user_data_len_dma(&nand,0,32);
        check_length_image(wide,0,32);
        sunxi_nfc_reset_user_data_len(&controller);
        sunxi_nfc_set_user_data_len_pio(&controller,len);
        u8 pio[]={len};
        check_length_image(pio,0,1);
        for(unsigned i=0;i<32;i++) mixed[i]=((i+len/4)%9)*4;
        nand.user_data_bytes=mixed;
        sunxi_nfc_set_user_data_len_dma(&nand,starts[first],1);
        check_length_image(mixed,starts[first],1);
        runs++;
    }
    for(unsigned n=0;n<ARRAY_SIZE(counts);n++)
    for(unsigned phase=0;phase<3;phase++) {
        nand.user_data_bytes=wide;
        sunxi_nfc_set_user_data_len_dma(&nand,0,32);
        for(unsigned i=0;i<32;i++) mixed[i]=((i+phase)%3)*4;
        nand.user_data_bytes=mixed;
        sunxi_nfc_set_user_data_len_dma(&nand,0,counts[n]);
        check_length_image(mixed,0,counts[n]);
        runs++;
    }
    /* A10/A23 have fixed metadata widths, not these length registers. */
    c=caps(false);
    u8 before[sizeof(controller.regs)];
    memcpy(before,controller.regs,sizeof(before));
    memset(mmio_writes,0,sizeof(mmio_writes));
    sunxi_nfc_reset_user_data_len(&controller);
    sunxi_nfc_set_user_data_len_pio(&controller,4);
    sunxi_nfc_set_user_data_len_dma(&nand,0,16);
    assert(!memcmp(before,controller.regs,sizeof(before)));
    for(unsigned i=0;i<ARRAY_SIZE(mmio_writes);i++) assert(!mmio_writes[i]);
    printf("persistent user-data lengths: %u PIO/DMA/word-boundary sequences; legacy no-MMIO check\n",runs);
}

/* Independent shift-register implementation, rather than the driver's formula. */
static u16 bit_reference(u16 seed, unsigned count)
{
    bool bits[15];
    for (unsigned j = 0; j < 15; j++) bits[j] = (seed >> j) & 1;
    while (count--) {
        bool feedback = bits[0] != bits[1];
        for (unsigned j = 0; j < 14; j++) bits[j] = bits[j + 1];
        bits[14] = feedback;
    }
    u16 result = 0;
    for (unsigned j = 0; j < 15; j++) result |= (u16)bits[j] << j;
    return result;
}

static void randomizer(void)
{
    for (unsigned seed = 0; seed <= UINT16_MAX; seed++) {
        assert(sunxi_nfc_randomizer_step(seed, 0) == (seed & 0x7fff));
        assert(sunxi_nfc_randomizer_step(seed, 1) == bit_reference(seed, 1));
        assert(sunxi_nfc_randomizer_step(seed, 8) == bit_reference(seed, 8));
    }
    for (unsigned i = 0; i < 128; i++) {
        assert(sunxi_nfc_randomizer_ecc512_seeds[i] ==
               bit_reference(sunxi_nfc_randomizer_page_seeds[i], 512 * 8 + 15));
        assert(sunxi_nfc_randomizer_ecc1024_seeds[i] ==
               bit_reference(sunxi_nfc_randomizer_page_seeds[i], 1024 * 8 + 15));
        for (unsigned len = 0; len <= 32; len++) {
            u8 buf[34], original[34];
            for (unsigned j = 0; j < sizeof(buf); j++) buf[j] = j * 37 + i;
            memcpy(original, buf, sizeof(buf));
            sunxi_nfc_randomize_buf(sunxi_nfc_randomizer_page_seeds[i], buf + 1, len);
            assert(buf[0] == original[0] && buf[len + 1] == original[len + 1]);
            for (unsigned j = 0; j < len; j++)
                assert(buf[j + 1] == (u8)(original[j + 1] ^
                       bit_reference(sunxi_nfc_randomizer_page_seeds[i], j * 8)));
            sunxi_nfc_randomize_buf(sunxi_nfc_randomizer_page_seeds[i], buf + 1, len);
            assert(!memcmp(original, buf, sizeof(buf)));
        }
    }
    struct nand_chip nand = chip(2, 56, 128);
    for (unsigned size = 512; size <= 1024; size *= 2)
    for (unsigned ppb = 32; ppb <= 256; ppb *= 2) {
        nand.mtd.erasesize = nand.mtd.writesize * ppb;
        nand.mtd.ecc_step_size = size;
        for (unsigned page = 0; page < ppb * 2; page++) {
            unsigned idx = page % min(ppb, 128);
            u16 ecc_seed = size == 512 ? sunxi_nfc_randomizer_ecc512_seeds[idx] :
                                        sunxi_nfc_randomizer_ecc1024_seeds[idx];
            assert(sunxi_nfc_randomizer_state(&nand, page, false) == sunxi_nfc_randomizer_page_seeds[idx]);
            assert(sunxi_nfc_randomizer_state(&nand, page, true) == ecc_seed);
            u8 bbm[4] = {0x00, 0xa5, 0x55, 0x66};
            sunxi_nfc_randomize_bbm(&nand, page, bbm);
            assert(bbm[0] == (u8)ecc_seed && bbm[1] == (u8)(0xa5 ^ bit_reference(ecc_seed, 8)));
            sunxi_nfc_randomize_bbm(&nand, page, bbm);
            assert(!memcmp(bbm, (u8[]){0x00, 0xa5, 0x55, 0x66}, 4));
        }
    }
    for (unsigned value = 0; value < 256; value++) {
        u8 bytes[] = {value, value ^ 0xa5, value ^ 0x5a, 255 - value}, result[4];
        sunxi_nfc_user_data_to_buf(sunxi_nfc_buf_to_user_data(bytes), result);
        assert(!memcmp(bytes, result, 4));
    }
    puts("randomizer: all 65536 seeds, both ECC seed tables, byte transforms, BBM symmetry");
}

static void predicates(void)
{
    struct nand_chip nand = chip(2, 56, 128);
    unsigned checked = 0;
    for (unsigned bitmap = 0; bitmap < 1024; bitmap++)
    for (unsigned value = 0; value < 255; value += 17) {
        u8 spare[10];
        for (unsigned i = 0; i < 10; i++) spare[i] = bitmap & BIT(i) ? 255 : value;
        bool expected = (bitmap & 1) && __builtin_popcount(bitmap) >= 9;
        assert(sun50i_h6_nfc_spare_is_erased(&nand, spare, 0) == expected);
        for (unsigned size = 512; size <= 1024; size *= 2)
        for (unsigned ppb = 64; ppb <= 256; ppb *= 2) {
            nand.ecc.size = size;
            nand.mtd.erasesize = ppb * nand.mtd.writesize;
            const unsigned pages[] = {0, 1, 63, 64, 126, 127, 128, 255, 256, 511};
            for (unsigned j = 0; j < ARRAY_SIZE(pages); j++) {
                unsigned bp = pages[j] % ppb;
                bool special = size == 1024 && (bp == 0 || bp % 128 == 127);
                expected = (bitmap & 255) == 255 || (special && (bitmap & 15) == 15);
                assert(sun4i_a10_nfc_spare_is_erased(&nand, spare, pages[j]) == expected);
                checked++;
            }
        }
    }
    printf("erased predicate truth-table checks: %u\n", checked);
}

static void load_status(unsigned first, unsigned count, const u8 *kinds, bool variable)
{
    struct caps *c = controller.caps;
    u32 errors = 0, patterns = 0, ids = 0;
    memset(controller.regs, 0, sizeof(controller.regs));
    memset(controller.regs + NFC_RAM0_BASE, 0x4b, 1024);
    for (unsigned i = 0; i < count; i++) {
        unsigned kind = kinds[first + i];
        if (kind >= 3) errors |= BIT(i);
        if (kind == 1 || kind == 2 || kind == 4 || kind == 5) patterns |= BIT(i);
        if (kind == 2 || kind == 5) ids |= BIT(i);
        controller.regs[c->reg_ecc_err_cnt + i] = first + i + 1;
    }
    for (unsigned i = 0; i < 128; i++) controller.regs[c->reg_user_data + i] = 0x39;
    writel(variable ? errors : errors | (patterns << 16), controller.regs + NFC_REG_ECC_ST);
    if (variable) writel(patterns, controller.regs + c->reg_pat_found);
    writel(ids, controller.regs + c->reg_pat_id);
}

static void ecc_results(void)
{
    unsigned checked = 0;
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned dma = 0; dma < 2; dma++)
    for (unsigned random = 0; random < 2; random++)
    for (unsigned want_oob = 0; want_oob < 2; want_oob++)
    for (unsigned erase = 0; erase < 2; erase++)
    for (unsigned input = 0; input < 216; input++) {
        struct caps c = caps(variable);
        controller.caps = &c;
        u8 lengths[3] = {32, 4, 0}, kinds[3];
        unsigned temp = input;
        for (unsigned i = 0; i < 3; i++) { kinds[i] = temp % 6; temp /= 6; }
        struct nand_chip nand = chip(3, 56, 256);
        nand.user_data_bytes = variable ? lengths : NULL;
        nand.options = random ? NAND_NEED_SCRAMBLING : 0;
        struct sunxi_nfc_oob_ops ops = { .transform_bbm = NULL };
        nand.oob_ops = &ops;
        struct sunxi_nfc_ecc_read read = {
            .buf = main_data, .page = 17, .dma = dma, .oob_required = want_oob,
        };
        memset(main_data, 0x4b, sizeof(main_data));
        memset(oob_data, 0x93, sizeof(oob_data));
        unsigned corrected = 0, failed = 0, maximum = 0;
        bool erased = false;
        raw_calls = fail_raw_at = 0;
        raw_is_erased = erase; erased_flips = 1;
        for (unsigned first = 0; first < 3;) {
            unsigned count = dma ? 3 : 1;
            load_status(first, count, kinds, variable);
            /* Non-randomized DMA delivers literal patterns directly. */
            for (unsigned i = first; i < first + count; i++)
                if (dma && !random && (kinds[i] == 1 || kinds[i] == 2))
                    memset(main_data + i * 1024, kinds[i] == 1 ? 0 : 255, 1024);
            struct sunxi_nfc_ecc_snapshot snapshot;
            sunxi_nfc_hw_ecc_read_status(&nand, &snapshot);
            assert(!sunxi_nfc_oob_read_batch(&nand, &read, &snapshot, first, count));
            first += count;
        }
        for (unsigned i = 0; i < 3; i++) {
            int expected = -1;
            if (kinds[i] >= 3) {
                if (erase) { corrected++; maximum = max(maximum, 1); erased = true; expected = 255; }
                else failed++;
            } else if (kinds[i]) {
                expected = kinds[i] == 1 ? 0 : 255;
                erased |= expected == 255;
            } else {
                corrected += i + 1;
                maximum = max(maximum, i + 1);
            }
            if (expected >= 0) {
                assert(!memchr_inv(main_data + 1024 * i, expected, 1024));
                if (want_oob || kinds[i] >= 3)
                    assert(!memchr_inv(oob_data + sunxi_get_oob_offset(&nand, &nand.ecc, i),
                                     expected, 56 + sunxi_nfc_user_data_sz(&nand, i)));
            }
            if (!kinds[i] && want_oob)
                assert(!memchr_inv(oob_data + sunxi_get_oob_offset(&nand, &nand.ecc, i),
                                  0x39, sunxi_nfc_user_data_sz(&nand, i)));
        }
        assert(nand.mtd.ecc_stats.corrected == corrected && nand.mtd.ecc_stats.failed == failed);
        assert(read.max_bitflips == maximum && read.has_erased_chunk == erased);
        assert(!memchr_inv(main_data + 3072, 0x4b, sizeof(main_data) - 3072));
        assert(!memchr_inv(oob_data + 256, 0x93, sizeof(oob_data) - 256));
        checked++;
    }
    printf("generic ECC pattern/error/batch matrix: %u\n", checked);
}

static void columns_and_faults(void)
{
    struct caps c = caps(true);
    controller.caps = &c;
    struct nand_chip nand = chip(2, 56, 128);
    u8 lengths[2] = {8, 0};
    nand.user_data_bytes = lengths;
    nand.mtd.writesize = 512;
    raw_calls = fail_raw_at = page_reads = column_reads = 0;
    assert(!sunxi_nfc_read_column(&nand, 3, 512, oob_data, 8));
    assert(page_reads == 1 && column_reads == 0);
    nand.mtd.writesize = 2048;
    assert(!sunxi_nfc_read_column(&nand, 3, 2048, oob_data, 8));
    assert(page_reads == 1 && column_reads == 1);
    unsigned end = sunxi_get_oob_offset(&nand, &nand.ecc, 2);
    int cur = nand.mtd.writesize + end;
    raw_calls = 0; column = cur;
    assert(!sunxi_nfc_hw_ecc_read_extra_oob(&nand, oob_data, &cur, false, 3));
    assert(raw_calls == 1 && cur == (int)(nand.mtd.writesize + nand.mtd.oobsize));
    cur = -1; raw_calls = 0;
    assert(!sunxi_nfc_hw_ecc_read_extra_oob(&nand, oob_data, &cur, false, 3));
    assert(raw_calls == 2);
    for (unsigned failure = 1; failure <= 3; failure++) {
        nand.options = NAND_NEED_SCRAMBLING;
        unsigned maxflips = 0;
        nand.mtd.ecc_stats.corrected = nand.mtd.ecc_stats.failed = 0;
        raw_calls = 0; fail_raw_at = failure;
        int ret = sunxi_nfc_hw_ecc_read_error(&nand, main_data, 0, oob_data, 2048, 8, &maxflips, 3);
        if (failure <= 2) {
            assert(ret == -ETIMEDOUT);
            assert(!nand.mtd.ecc_stats.corrected && !nand.mtd.ecc_stats.failed);
        }
    }
    fail_raw_at = 0;
    puts("column selection, tail cursor and raw-reread error propagation checks passed");

    unsigned finish_cases = 0;
    for (unsigned want = 0; want < 2; want++)
    for (unsigned erased = 0; erased < 2; erased++)
    for (unsigned cursor = 0; cursor < 3; cursor++)
    for (unsigned tail = 0; tail <= 8; tail += 8)
    for (unsigned failure = 0; failure <= 2; failure++) {
        nand.mtd.oobsize = end + tail;
        struct sunxi_nfc_ecc_read read = {
            .page = 3, .oob_required = want, .has_erased_chunk = erased,
            .max_bitflips = 7,
        };
        cur = cursor == 1 ? nand.mtd.writesize + end : -1;
        column = nand.mtd.writesize + end;
        raw_calls = plain_read_calls = random_read_calls = 0;
        fail_raw_at = failure;
        memset(oob_data, 0x93, sizeof(oob_data));
        unsigned calls = want && tail ? (cursor == 1 ? 1 : 2) : 0;
        bool error = failure && failure <= calls;
        int ret = sunxi_nfc_oob_finish_read(&nand, &read, cursor ? &cur : NULL);
        assert(ret == (error ? -ETIMEDOUT : 7));
        assert(raw_calls == (error ? failure : calls));
        assert(!memchr_inv(oob_data, 0x93, end));
        assert(!memchr_inv(oob_data + end + tail, 0x93, sizeof(oob_data) - end - tail));
        if (calls && !error) {
            assert(plain_read_calls == erased && random_read_calls == !erased);
            assert(!memchr_inv(oob_data + end, 0xa7, tail));
            if (cursor) assert(cur == (int)(nand.mtd.writesize + nand.mtd.oobsize));
        } else {
            assert(!memchr_inv(oob_data + end, 0x93, tail));
        }
        finish_cases++;
    }
    fail_raw_at = 0;
    printf("generic OOB finish: %u tail/cursor/representation/failure cases\n", finish_cases);
}

static void vendor_results(void)
{
    unsigned checked = 0;
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned dma = 0; dma < 2; dma++)
    for (unsigned want_oob = 0; want_oob < 2; want_oob++)
    for (unsigned erased = 0; erased < 2; erased++)
    for (unsigned input = 0; input < 216; input++) {
        struct caps c = caps(variable);
        controller.caps = &c;
        u8 kinds[3], lengths[] = {12, 0, 0};
        unsigned temp = input;
        for (unsigned i = 0; i < 3; i++) { kinds[i] = temp % 6; temp /= 6; }
        struct nand_chip nand = chip(3, 56, 256);
        nand.user_data_bytes = variable ? lengths : NULL;
        struct sunxi_nfc_oob_ops ops = {0}; /* Firmware format has no plain-BBM transform. */
        nand.oob_ops = &ops;
        struct sunxi_nfc_ecc_read read = {
            .buf = main_data, .page = 17, .dma = dma, .oob_required = want_oob,
        };
        memset(main_data, 0x4b, sizeof(main_data));
        memset(oob_data, 0x93, sizeof(oob_data));
        raw_calls = fail_raw_at = 0;
        unsigned exceptions = 0, zeros = 0, corrected = 0, maximum = 0;
        for (unsigned first = 0; first < 3;) {
            unsigned count = dma ? 3 : 1;
            load_status(first, count, kinds, variable);
            struct sunxi_nfc_ecc_snapshot snapshot;
            sunxi_nfc_hw_ecc_read_status(&nand, &snapshot);
            assert(!sunxi_nfc_allwinner_oob_read_batch(&nand, &read, &snapshot, first, count));
            first += count;
        }
        for (unsigned i = 0; i < 3; i++) {
            if (kinds[i]) exceptions |= BIT(i);
            else { corrected += i + 1; maximum = max(maximum, i + 1); }
            if (kinds[i] == 1 || kinds[i] == 4) zeros |= BIT(i);
            unsigned off = sunxi_get_oob_offset(&nand, &nand.ecc, i);
            unsigned len = sunxi_nfc_user_data_sz(&nand, i);
            assert(!memchr_inv(oob_data + off, 0x39, len));
            /* Create ECC-decoded metadata whose re-encoding has a known predicate result. */
            memset(oob_data + off, erased ? 255 : 0, len);
            sunxi_nfc_randomize_buf(sunxi_nfc_randomizer_state(&nand, read.page, true),
                                   oob_data + off, len);
        }
        assert(read.allwinner_oob.error_steps == exceptions);
        assert(read.allwinner_oob.zero_steps == zeros);
        assert(read.allwinner_oob.corrected == corrected);
        assert(read.allwinner_oob.max_bitflips == maximum);
        assert(sunxi_nfc_allwinner_oob_spare_is_erased(&nand, read.page) == !!erased);
        u8 saved[256];
        memcpy(saved, oob_data, sizeof(saved));
        int ret = sunxi_nfc_allwinner_oob_finish_read(&nand, &read, NULL);
        bool badzero = zeros == 7, blank = !badzero && exceptions && erased;
        assert(ret == (int)((badzero || blank) ? 0 : maximum));
        assert(nand.mtd.ecc_stats.failed == (badzero ? 3 : blank ? 0 : (unsigned)__builtin_popcount(exceptions)));
        assert(nand.mtd.ecc_stats.corrected == ((badzero || blank) ? 0 : corrected));
        if (badzero || blank) {
            assert(!memchr_inv(main_data, badzero ? 0 : 255, 3072));
            assert(!memchr_inv(oob_data, badzero ? 0 : 255, 256));
            assert(!raw_calls);
        } else {
            assert(!memchr_inv(main_data, 0x4b, 3072));
            for (unsigned i = 0; i < 3; i++) {
                unsigned off = sunxi_get_oob_offset(&nand, &nand.ecc, i);
                unsigned len = sunxi_nfc_user_data_sz(&nand, i);
                assert(!memcmp(oob_data + off, saved + off, len));
            }
            assert(!!raw_calls == !!want_oob);
        }
        assert(!memchr_inv(main_data + 3072, 0x4b, sizeof(main_data) - 3072));
        assert(!memchr_inv(oob_data + 256, 0x93, sizeof(oob_data) - 256));
        checked++;
    }
    printf("vendor classification/precedence/decoded-OOB matrix: %u\n", checked);
}

static void unprotected_oob(void)
{
    unsigned checked = 0;
    struct caps c = caps(true);
    controller.caps = &c;
    const u8 layouts[][5] = {{16,0,0,0,0}, {32,16,4,0,0}, {4,4,4,4,4}};
    for (unsigned layout = 0; layout < ARRAY_SIZE(layouts); layout++)
    for (unsigned dma = 0; dma < 2; dma++)
    for (unsigned tail = 0; tail < 17; tail++)
    for (unsigned fault = 0; fault <= 5; fault++) {
        struct nand_chip nand = chip(5, 56, 0);
        nand.user_data_bytes = (u8 *)layouts[layout];
        unsigned end = sunxi_get_oob_offset(&nand, &nand.ecc, 5);
        nand.mtd.oobsize = end + tail;
        raw_calls = 0; fail_raw_at = fault;
        memset(oob_data, 0x93, sizeof(oob_data));
        int ret = sunxi_nfc_allwinner_oob_read_unprotected(&nand, dma, 17);
        assert(ret == ((fault && raw_calls >= fault) ? -ETIMEDOUT : 0));
        for (unsigned i = 0; i < 5; i++) {
            unsigned off = sunxi_get_oob_offset(&nand, &nand.ecc, i);
            unsigned len = layouts[layout][i];
            assert(!memchr_inv(oob_data + off, 0x93, len));
            if (ret) continue;
            off += len;
            u16 state = bit_reference(sunxi_nfc_randomizer_state(&nand, 17, true), len * 8 + 15);
            for (unsigned j = 0; j < 56; j++)
                assert(oob_data[off + j] == (u8)(0xa7 ^ (dma ? 0 : bit_reference(state, 8 * j))));
        }
        if (!ret) {
            u16 state = bit_reference(sunxi_nfc_randomizer_state(&nand, 17, false), 15);
            for (unsigned j = 0; j < tail; j++)
                assert(oob_data[end + j] == (u8)(0xa7 ^ bit_reference(state, 8 * j)));
        }
        assert(!memchr_inv(oob_data + end + tail, 0x93, sizeof(oob_data) - end - tail));
        checked++;
    }
    fail_raw_at = 0;
    printf("unprotected OOB coalescing/representation/error checks: %u\n", checked);
}

int main(void)
{
    geometry();
    length_sequence_checks();
    randomizer();
    predicates();
    ecc_results();
    vendor_results();
    unprotected_oob();
    columns_and_faults();
    control_flow();
    attach_geometry();
    snapshot_cache();
    bitmap_boundaries();
    attach_policy();
    metadata_identity();
    format_roundtrip();
    compatibility();
    codec_bridge();
    oob_pipeline();
    puts("ALL C CHECKS PASSED");
    return 0;
}

static void snapshot_cache(void)
{
    for (unsigned variable = 0; variable < 2; variable++) {
        struct caps c = caps(variable);
        struct nand_chip nand = chip(8, 56, 512);
        struct sunxi_nfc_ecc_snapshot snapshot;
        controller.caps = &c;
        memset(controller.regs, 0, sizeof(controller.regs));
        memset(mmio_reads, 0, sizeof(mmio_reads));
        writel(field_prep(c.pat_found_mask, 1), controller.regs + c.reg_pat_found);
        sunxi_nfc_hw_ecc_read_status(&nand, &snapshot);
        assert(snapshot.pattern_id_valid && !snapshot.pattern_id);
        sunxi_nfc_hw_ecc_read_pattern_id(&nand, &snapshot);
        sunxi_nfc_hw_ecc_read_pattern_id(&nand, &snapshot);
        assert(mmio_reads[c.reg_pat_id / 4] == 1);
        writel(0x04030201, controller.regs + c.reg_ecc_err_cnt);
        writel(0x08070605, controller.regs + c.reg_ecc_err_cnt + 4);
        for (unsigned i = 0; i < 8; i++)
            assert(sunxi_nfc_hw_ecc_read_count(&nand, &snapshot, i) == i + 1);
        assert(mmio_reads[c.reg_ecc_err_cnt / 4] == 1);
        assert(mmio_reads[c.reg_ecc_err_cnt / 4 + 1] == 1);
        /* Every new PIO step or DMA batch must invalidate the previous word. */
        writel(0x14131211, controller.regs + c.reg_ecc_err_cnt);
        sunxi_nfc_hw_ecc_read_status(&nand, &snapshot);
        assert(sunxi_nfc_hw_ecc_read_count(&nand, &snapshot, 0) == 17);
        assert(mmio_reads[c.reg_ecc_err_cnt / 4] == 2);
    }
    puts("snapshot cache: zero pattern IDs, four-byte counter words, per-transfer invalidation");
}

static void bitmap_boundaries(void)
{
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned mode = 0; mode < 4; mode++) {
        struct caps c = caps(variable);
        unsigned n = c.max_ecc_steps;
        u8 user[32] = {16}, kinds[32] = {0};
        struct nand_chip nand = chip(n, 56, n * 60);
        struct sunxi_nfc_ecc_read read = {.buf = main_data, .page = 17, .dma = true};
        struct sunxi_nfc_ecc_snapshot snapshot;
        struct sunxi_nfc_oob_ops ops = {0};
        controller.caps = &c;
        nand.user_data_bytes = variable ? user : NULL;
        nand.oob_ops = &ops;
        if (!mode) memset(kinds, 1, n);
        if (mode == 1) kinds[n - 1] = 1;
        if (mode == 2) kinds[n - 1] = 3;
        memset(main_data, 0x5a, sizeof(main_data));
        memset(oob_data, 0xa5, sizeof(oob_data));
        load_status(0, n, kinds, variable);
        sunxi_nfc_hw_ecc_read_status(&nand, &snapshot);
        assert(!sunxi_nfc_allwinner_oob_read_batch(&nand, &read, &snapshot, 0, n));
        u32 errors = !mode ? GENMASK(n - 1, 0) : mode == 3 ? 0 : BIT(n - 1);
        assert(read.allwinner_oob.error_steps == errors);
        assert(read.allwinner_oob.zero_steps == (mode == 2 ? 0 : errors));
        /* Make the firmware spare heuristic false for partial-pattern cases. */
        oob_data[0] = sunxi_nfc_randomizer_state(&nand, 17, true);
        int ret = sunxi_nfc_allwinner_oob_finish_read(&nand, &read, NULL);
        assert(ret >= 0);
        if (!mode) {
            assert(!memchr_inv(main_data, 0, nand.mtd.writesize));
            assert(!memchr_inv(oob_data, 0, nand.mtd.oobsize));
            assert(nand.mtd.ecc_stats.failed == n);
        } else {
            assert(!memchr_inv(main_data, 0x5a, nand.mtd.writesize));
            assert(nand.mtd.ecc_stats.failed == (mode == 3 ? 0 : 1));
        }
        assert(main_data[nand.mtd.writesize] == 0x5a);
        assert(oob_data[nand.mtd.oobsize] == 0xa5);
    }
    puts("ECC bitmap boundaries: 16/32 steps, highest bit, full-zero mask");
}
