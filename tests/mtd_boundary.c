/* SPDX-License-Identifier: GPL-2.0-or-later */
extern void driver_bitflips(unsigned);
static unsigned mtd_profiles, mtd_requests, mtd_witnesses, admission_checks, admission_eof;
static struct mtd_info outer, inner;

static struct mtd_info *partitions(unsigned depth)
{
    outer = chip.mtd;
    outer.parent = &chip.mtd;
    outer.part.offset = 248 * PAGE;
    outer.size = 16 * PAGE;
    outer.ecc_stats = (struct mtd_ecc_stats){0};
    inner = outer;
    inner.parent = &outer;
    inner.part.offset = 4 * PAGE;
    inner.size = 8 * PAGE;
    return depth == 2 ? &inner : depth == 1 ? &outer : &chip.mtd;
}

static void assert_idle(void)
{
    assert(!device_locked && chip.selected == -1);
}

static void rejected(struct mtd_info *m, loff_t offset, struct mtd_oob_ops op, bool write, int error)
{
    unsigned before = callbacks;
    op.retlen = op.oobretlen = 123;
    assert((write ? mtd_write_oob(m, offset, &op) : mtd_read_oob(m, offset, &op)) == error);
    assert(op.retlen == 0 && op.oobretlen == 0 && callbacks == before);
    assert_idle();
    mtd_requests++;
}

static void public_requests(void)
{
    u8 data[3 * PAGE], output[3 * PAGE + 2], spare[3 * SPARE], got[3 * SPARE + 2];
    for (unsigned format = 0; format < 3; format++)
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned dma = 0; dma < 2; dma++)
    for (unsigned depth = 0; depth < 3; depth++) {
        setup(format, variable);
        use_dma = dma;
        chip.subpage_read = true; /* sunxi advertises NAND_SUBPAGE_READ */
        struct mtd_info *m = partitions(depth);
        loff_t start = (loff_t)255 * PAGE - mtd_get_master_ofs(m, 0);
        unsigned avail = chip.mtd.oobavail;
        for (unsigned i = 0; i < sizeof(data); i++) data[i] = i * 19 + (i >> 8) * 7;
        for (unsigned i = 0; i < sizeof(spare); i++) spare[i] = i * 13 + 37;
        struct mtd_oob_ops op = {.mode=MTD_OPS_AUTO_OOB, .datbuf=data, .len=sizeof(data),
                                .oobbuf=spare, .ooblen=3 * avail};
        assert(!mtd_write_oob(m, start, &op));
        assert(op.retlen == sizeof(data) && op.oobretlen == 3 * avail);
        memset(output, 0xa5, sizeof(output));
        memset(got, 0xa5, sizeof(got));
        op = (struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB, .datbuf=output + 1, .len=sizeof(data) - 31,
                                .oobbuf=got + 1, .ooblen=3 * avail};
        assert(!mtd_read_oob(m, start + 17, &op));
        assert(op.retlen == sizeof(data) - 31 && !memcmp(output + 1, data + 17, op.retlen));
        assert(op.oobretlen == 3 * avail && !memcmp(got + 1, spare, op.oobretlen));
        assert(output[0] == 0xa5 && output[1 + op.retlen] == 0xa5);
        assert(got[0] == 0xa5 && got[1 + op.oobretlen] == 0xa5);
        assert_idle();
        mtd_requests += 2;

        /* Both ordinary data and OOB bounds are checked before any page callback. */
        op = (struct mtd_oob_ops){.datbuf=output, .len=1};
        rejected(m, -1, op, false, -EINVAL);
        rejected(m, m->size, op, false, -EINVAL);
        rejected(m, m->size + PAGE, op, false, -EINVAL);
        rejected(m, m->size, op, true, -EINVAL);
        if (EXPECT_DATA_BOUNDS) {
            op = (struct mtd_oob_ops){.datbuf=output, .len=SIZE_MAX};
            rejected(m, 1, op, false, -EINVAL);
            rejected(m, 1, op, true, -EINVAL);
        }
        op = (struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB, .oobbuf=got, .ooblen=1, .ooboffs=avail};
        rejected(m, start, op, false, -EINVAL);
        op.ooboffs = 0;
        op.ooblen = avail + 1;
        rejected(m, m->size - PAGE, op, false, -EINVAL);
        op = (struct mtd_oob_ops){.mode=99, .datbuf=output, .len=PAGE};
        rejected(m, start, op, false, -EOPNOTSUPP);
        op = (struct mtd_oob_ops){.datbuf=data, .len=PAGE};
        m->flags &= ~MTD_WRITEABLE;
        rejected(m, start, op, true, -EROFS);
        m->flags |= MTD_WRITEABLE;
        rejected(m, start + 1, op, true, -EINVAL);

        /* A later transport failure reports only the completed data/OOB prefix. */
        driver_faults(0, -1);
        op = (struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB, .datbuf=output, .len=3 * PAGE,
                                .oobbuf=got, .ooblen=3 * avail};
        assert(mtd_read_oob(m, start, &op) == -EIO);
        assert(op.retlen == PAGE && op.oobretlen == avail);
        assert(!memcmp(output, data, PAGE) && !memcmp(got, spare, avail));
        assert_idle();
        driver_faults(-1, -1);
        size_t returned;
        assert(!mtd_read(m, start, sizeof(data), &returned, output));
        assert(returned == sizeof(data) && !memcmp(data, output, returned));
        mtd_requests += 2;

        /* Exercise public mtd_write() and input-length normalization. */
        driver_erase(255, 1);
        assert(!mtd_write(m, start, PAGE, &returned, data) && returned == PAGE);
        op = (struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB, .len=SIZE_MAX,
                                .oobbuf=got, .ooblen=1};
        assert(!mtd_read_oob(m, start, &op) && op.len == 0 && op.oobretlen == 1);
        op = (struct mtd_oob_ops){.datbuf=output, .len=1, .ooblen=SIZE_MAX};
        assert(!mtd_read_oob(m, start, &op) && op.ooblen == 0 && op.retlen == 1);
        assert_idle();
        mtd_requests += 3;
        driver_finish();
        mtd_profiles++;
    }
}

static void ecc_accounting(void)
{
    u8 data[PAGE], output[PAGE];
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned dma = 0; dma < 2; dma++)
    for (unsigned depth = 0; depth < 3; depth++) {
        setup(0, variable);
        use_dma = dma;
        struct mtd_info *m = partitions(depth);
        loff_t start = (loff_t)255 * PAGE - mtd_get_master_ofs(m, 0);
        memset(data, 0x96, sizeof(data));
        size_t returned;
        struct mtd_req_stats untouched = {99, 99, 99};
        struct mtd_oob_ops write = {.datbuf=data, .len=PAGE, .stats=&untouched};
        assert(!mtd_write_oob(m, start, &write) && write.retlen == PAGE);
        assert(untouched.max_bitflips == 99 && untouched.corrected_bitflips == 99 &&
               untouched.uncorrectable_errors == 99);
        /* Admission errors leave request stats alone, but clear return lengths. */
        struct mtd_oob_ops invalid = {.datbuf=output, .len=1, .stats=&untouched};
        rejected(m, m->size, invalid, false, -EINVAL);
        assert(untouched.max_bitflips == 99 && untouched.corrected_bitflips == 99 &&
               untouched.uncorrectable_errors == 99);
        m->bitflip_threshold = 4;
        for (unsigned bits = 0; bits <= 5; bits++) {
            driver_bitflips(bits);
            struct mtd_ecc_stats old_master = chip.mtd.ecc_stats, old_outer = outer.ecc_stats,
                                 old_inner = inner.ecc_stats;
            struct mtd_req_stats stats = {99, 99, 99};
            struct mtd_oob_ops op = {.datbuf=output, .len=PAGE, .stats=&stats};
            int ret = mtd_read_oob(m, start, &op);
            assert(ret == (bits >= 4 ? -EUCLEAN : 0));
            assert(op.retlen == PAGE && !memcmp(output, data, PAGE));
            assert(stats.max_bitflips == bits && stats.uncorrectable_errors == 0);
            unsigned delta = chip.mtd.ecc_stats.corrected - old_master.corrected;
            assert(stats.corrected_bitflips == delta && (delta > 0) == (bits > 0));
            assert(outer.ecc_stats.corrected - old_outer.corrected == (depth ? delta : 0));
            assert(inner.ecc_stats.corrected - old_inner.corrected == (depth == 2 ? delta : 0));
            assert_idle();
            mtd_requests++;
        }
        driver_bitflips(0);
        driver_faults(-1, 255);
        struct mtd_req_stats stats = {99, 99, 99};
        unsigned old_failed = chip.mtd.ecc_stats.failed;
        unsigned outer_failed = outer.ecc_stats.failed, inner_failed = inner.ecc_stats.failed;
        struct mtd_oob_ops op = {.datbuf=output, .len=PAGE, .stats=&stats};
        assert(mtd_read_oob(m, start, &op) == -EBADMSG && op.retlen == PAGE);
        assert(stats.uncorrectable_errors == chip.mtd.ecc_stats.failed - old_failed);
        assert(stats.uncorrectable_errors > 0 && stats.max_bitflips == 0);
        assert(outer.ecc_stats.failed - outer_failed == (depth ? stats.uncorrectable_errors : 0));
        assert(inner.ecc_stats.failed - inner_failed == (depth == 2 ? stats.uncorrectable_errors : 0));
        assert_idle();
        driver_faults(-1, -1);
        mtd_requests++;
        /* Reusing the stats object must not retain the previous failed count. */
        assert(!mtd_read_oob(m, start, &op) && op.retlen == PAGE);
        assert(!stats.uncorrectable_errors && !stats.corrected_bitflips && !stats.max_bitflips);
        assert(!mtd_read(m, start, PAGE, &returned, output) && returned == PAGE);
        assert_idle();
        mtd_requests += 3; /* includes the successful write above */
        driver_finish();
    }
}

/* Stop impossible host-size requests at admission; never use their buffers. */
static void admission_arithmetic(void)
{
    struct mtd_info device = {.writesize=PAGE, .writesize_shift=11,
                              .oobsize=SPARE, .oobavail=SPARE / 2, .size=8 * PAGE};
    struct mtd_info *m = &device;
    /* Both implementations of the real inline divider agree on this geometry. */
    for (unsigned n = 0; n <= 2 * PAGE; n++) {
        device.writesize_shift = 11;
        unsigned shifted = mtd_div_by_ws(n, m);
        device.writesize_shift = 0;
        assert(mtd_div_by_ws(n, m) == shifted && shifted == n / PAGE);
    }
    device.writesize_shift = 11;
    u8 byte;
    struct mtd_oob_ops op = {.datbuf=&byte, .len=SIZE_MAX};
    assert(sizeof(size_t) == 8);
    assert(mtd_check_oob_ops(m, 1, &op) == (EXPECT_DATA_BOUNDS ? -EINVAL : 0));
    assert(op.len > m->size - 1);
    mtd_witnesses += !EXPECT_DATA_BOUNDS;
    /* Exercise real 64-bit extremes without ever passing buffers to a driver. */
    const loff_t offsets[] = {-1, 0, 1, 8 * PAGE - 1, 8 * PAGE,
                              8 * PAGE + 1, INT64_MAX};
    const size_t lengths[] = {0, 1, 8 * PAGE - 1, 8 * PAGE,
                             8 * PAGE + 1, UINT32_MAX, INT64_MAX,
                             (size_t)INT64_MAX + 1, SIZE_MAX - 1, SIZE_MAX};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++)
    for (unsigned j = 0; j < sizeof(lengths) / sizeof(lengths[0]); j++)
    for (unsigned buffer = 0; buffer < 2; buffer++) {
        loff_t off = offsets[i];
        size_t length = buffer ? lengths[j] : 0;
        op = (struct mtd_oob_ops){.datbuf=buffer ? &byte : NULL, .len=lengths[j]};
        bool valid = off >= 0 && (u64)off <= m->size && length <= m->size - off;
        int ret = mtd_check_oob_ops(m, off, &op);
        assert(op.len == length);
        if ((ret == 0) != valid) {
            assert(!EXPECT_DATA_BOUNDS && !ret && !valid);
            assert(off >= 0 && length > SIZE_MAX - (size_t)off);
            mtd_witnesses++;
        }
        admission_checks++;
    }
    /* EOF has zero pages remaining; subtracting a nonzero OOB offset wraps. */
    op = (struct mtd_oob_ops){.mode=MTD_OPS_RAW, .oobbuf=&byte, .ooblen=1, .ooboffs=1};
    assert(mtd_check_oob_ops(m, m->size, &op) == (EXPECT_EOF_REJECTION ? -EINVAL : 0));
    op.ooboffs = 0;
    assert(mtd_check_oob_ops(m, m->size, &op) == -EINVAL);
    op.ooblen = 0;
    assert(!mtd_check_oob_ops(m, m->size, &op));

    /* A read-only partition may end inside a page (Sashiko's v1 review). */
    device.size++;
    op = (struct mtd_oob_ops){.mode=MTD_OPS_RAW, .oobbuf=&byte, .ooblen=1, .ooboffs=1};
    assert(!mtd_check_oob_ops(m, 8 * PAGE, &op));
    assert(mtd_div_by_ws(m->size, m) == mtd_div_by_ws(8 * PAGE, m));
    mtd_witnesses++;

    /* Small exhaustive admission matrix; no callbacks touch these buffers. */
    device = (struct mtd_info){.writesize=8, .writesize_shift=3,
                               .oobsize=4, .oobavail=2, .size=32};
    for (unsigned mode = 0; mode < 3; mode++)
    for (unsigned pointers = 0; pointers < 4; pointers++)
    for (int off = -1; off <= 33; off++)
    for (unsigned length = 0; length <= 33; length++)
    for (unsigned olen = 0; olen <= 17; olen++)
    for (unsigned ooff = 0; ooff <= 4; ooff++) {
        op = (struct mtd_oob_ops){.mode=mode, .datbuf=(pointers & 1) ? &byte : NULL,
            .oobbuf=(pointers & 2) ? &byte : NULL, .len=length, .ooblen=olen, .ooboffs=ooff};
        unsigned n = op.datbuf ? length : 0, on = op.oobbuf ? olen : 0;
        unsigned capacity = mode == MTD_OPS_AUTO_OOB ? 2 : 4;
        bool valid = off >= 0 && off <= 32 && n <= 32U - off;
        if (valid && on)
            valid = off < 32 && ooff < capacity &&
                    on + ooff <= (4U - (unsigned)off / 8) * capacity;
        int ret = mtd_check_oob_ops(m, off, &op);
        assert(op.len == n && op.ooblen == on);
        if ((ret == 0) != valid) {
            assert(!EXPECT_EOF_REJECTION);
            assert(!valid && !ret && off == 32 && n == 0 && on && ooff && ooff < capacity);
            admission_eof++;
        }
        admission_checks++;
    }
    assert(EXPECT_EOF_REJECTION ? admission_eof == 0 : admission_eof > 0);
}

static unsigned fallback_calls;
static int fallback_read(struct mtd_info *m, loff_t off, size_t n, size_t *done, u8 *data)
{
    assert(m == &chip.mtd && off == 252 * PAGE && n == 1);
    data[0] = 0x6e;
    *done = 1;
    fallback_calls++;
    return 3;
}
static int fallback_write(struct mtd_info *m, loff_t off, size_t n, size_t *done, const u8 *data)
{
    assert(m == &chip.mtd && off == 252 * PAGE && n == 1 && data[0] == 0x6e);
    *done = 1;
    fallback_calls++;
    return 0;
}
static void fallback_dispatch(void)
{
    /* Separate API-fixture dispatch test, not a claim of a sunxi _read path. */
    memset(&chip, 0, sizeof(chip));
    chip.selected = -1;
    chip.mtd = (struct mtd_info){.writesize=PAGE, .writesize_shift=11, .oobsize=SPARE,
        .size=(u64)PAGES*PAGE, .flags=MTD_WRITEABLE, .ecc_strength=32, .bitflip_threshold=4,
        ._read=fallback_read, ._write=fallback_write};
    struct mtd_info *m = partitions(2);
    u8 byte;
    size_t returned;
    assert(!mtd_read(m, 0, 1, &returned, &byte) && returned == 1 && byte == 0x6e);
    assert(!mtd_write(m, 0, 1, &returned, &byte) && returned == 1);
    m->bitflip_threshold = 3;
    assert(mtd_read(m, 0, 1, &returned, &byte) == -EUCLEAN);
    m->ecc_strength = 0;
    assert(!mtd_read(m, 0, 1, &returned, &byte));
    struct mtd_oob_ops op = {.datbuf=&byte, .len=1, .oobbuf=&byte, .ooblen=1};
    rejected(m, 0, op, false, -EOPNOTSUPP);
    rejected(m, 0, op, true, -EOPNOTSUPP);
    /* A device without OOB, such as SPI NOR, still permits data-only I/O. */
    m->oobsize = m->oobavail = 0;
    assert(!mtd_read(m, 0, 1, &returned, &byte) && returned == 1);
    assert(!mtd_write(m, 0, 1, &returned, &byte) && returned == 1);
    chip.mtd._read = NULL;
    chip.mtd._write = NULL;
    op.oobbuf = NULL;
    rejected(m, 0, op, false, -EOPNOTSUPP);
    rejected(m, 0, op, true, -EOPNOTSUPP);
    assert(fallback_calls == 6);
}

/* Reach real sunxi commands beyond a child boundary, but inside the master. */
static void eof_oob_witness(void)
{
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned dma = 0; dma < 2; dma++)
    for (unsigned raw = 0; raw < 2; raw++) {
        setup(0, variable);
        use_dma = dma;
        struct mtd_info *m = partitions(2);
        u8 data[PAGE];
        size_t returned;
        /* Establish a normal corrected operation before switching to RAW. */
        assert(!mtd_read(m, 0, PAGE, &returned, data) && returned == PAGE);
        u8 byte = 0x42, readback = 0;
        struct mtd_file_info info = {.mtd=m, .mode=raw ? MTD_FILE_MODE_RAW : 0};
        struct file file = {.private_data=&info};
        struct mtd_oob_ops op = {.mode=raw ? MTD_OPS_RAW : MTD_OPS_PLACE_OOB,
                                .oobbuf=&readback, .ooblen=1, .ooboffs=1};
        loff_t next_page = mtd_get_master_ofs(m, m->size);
        assert(!mtd_read_oob(&chip.mtd, next_page, &op) && op.oobretlen == 1);
        u8 original = readback;
        u32 oob_returned = 123;
        unsigned before = callbacks;
        /* The real ioctl helper derives ooboffs=1 and rounds start to EOF. */
        int ret = mtdchar_writeoob(&file, m, m->size + 1, 1, &byte, &oob_returned);
        assert(ret == (EXPECT_EOF_REJECTION ? -EINVAL : 0));
        assert(oob_returned == (EXPECT_EOF_REJECTION ? 0 : 1));
        assert(callbacks == before + (EXPECT_EOF_REJECTION ? 0 : 1));
        assert(!mtd_read_oob(&chip.mtd, next_page, &op));
        assert(readback == (EXPECT_EOF_REJECTION ? original : byte) && op.oobretlen == 1);
        readback = 0xa5;
        oob_returned = 123;
        before = callbacks;
        ret = mtdchar_readoob(&file, m, m->size + 1, 1, &readback, &oob_returned);
        assert(ret == (EXPECT_EOF_REJECTION ? -EINVAL : 0));
        assert(readback == (EXPECT_EOF_REJECTION ? 0xa5 : byte));
        assert(oob_returned == (EXPECT_EOF_REJECTION ? 0 : 1));
        assert(callbacks == before + (EXPECT_EOF_REJECTION ? 0 : 1));
        before = callbacks;
        assert(mtdchar_readoob(&file, m, m->size, 1, &readback, &oob_returned) == -EINVAL);
        assert(callbacks == before);
        assert_idle();
        if (!EXPECT_EOF_REJECTION)
            mtd_witnesses++;
        driver_finish();
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Reject an unfixed candidate before exercising extreme public requests. */
    admission_arithmetic();
    /* Existing end-to-end request/BBT tests now also use public MTD wrappers. */
    composed_main();
    public_requests();
    ecc_accounting();
    eof_oob_witness();
    fallback_dispatch();
    assert(boundary_warnings == 0);
    printf("MTD boundary: %u profiles, %u public requests, %u expected baseline witnesses\n",
           mtd_profiles, mtd_requests, mtd_witnesses);
    printf("Admission matrix: %u checks; %u exact EOF-underflow witnesses; fallback calls: %u\n",
           admission_checks, admission_eof, fallback_calls);
    puts("ALL MTD BOUNDARY CHECKS PASSED");
    return 0;
}
