/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Extracted page functions run unchanged. Faults are injected at their external
 * transport/API boundaries, not by replacing their own cleanup branches. */
static unsigned read_seen, pio_seen, expected_stop;

static int flow_read_batch(struct nand_chip *nand, struct sunxi_nfc_ecc_read *read,
                           struct sunxi_nfc_ecc_snapshot *snapshot,
                           int first, int count)
{
    assert(count && first + count <= expected_stop);
    for (unsigned i = first; i < first + count; i++) {
        read_seen |= BIT(i);
        if (!read->dma) pio_seen |= BIT(i);
    }
    /* Independent non-zero deltas exercise rollback of BOTH counters. The
     * actual ECC decoding/classification functions are tested in check.c. */
    nand->mtd.ecc_stats.corrected += count;
    nand->mtd.ecc_stats.failed += count;
    read->max_bitflips = 2;
    return 0;
}

static int flow_finish_read(struct nand_chip *nand, struct sunxi_nfc_ecc_read *read,
                            int *cur_off)
{
    if (flow_event(FP_READ)) return -ETIMEDOUT;
    return read->max_bitflips;
}

static const struct sunxi_nfc_oob_ops flow_ops = {
    .read_batch = flow_read_batch, .finish_read = flow_finish_read,
};

static void reset_flow(unsigned failure)
{
    memset(&flow, 0, sizeof(flow));
    flow.enabled = true;
    flow.fail_at = failure;
    memset(controller.regs, 0, sizeof(controller.regs));
    memset(main_data, 0x5a, sizeof(main_data));
    memset(oob_data, 0xa5, sizeof(oob_data));
    raw_calls = fail_raw_at = column = page_reads = column_reads = 0;
    read_seen = pio_seen = 0;
}

static int write_case(struct nand_chip *nand, unsigned operation, bool want_oob)
{
    switch (operation) {
    case 0: return sunxi_nfc_hw_ecc_write_page(nand, main_data, want_oob, 17);
    case 1: return sunxi_nfc_hw_ecc_write_page_dma(nand, main_data, want_oob, 17);
    case 2: return sunxi_nfc_hw_ecc_write_subpage(nand, 1025, 1024, main_data, want_oob, 17);
    case 3:
        nand->ecc.write_page = sunxi_nfc_hw_ecc_write_page;
        return sunxi_nfc_hw_ecc_write_oob(nand, 17);
    default:
        nand->ecc.write_page = sunxi_nfc_hw_ecc_write_page_dma;
        return sunxi_nfc_hw_ecc_write_oob(nand, 17);
    }
}

static int read_case(struct nand_chip *nand, unsigned operation)
{
    switch (operation) {
    case 0: return sunxi_nfc_hw_ecc_read_page(nand, main_data, 1, 17);
    case 1: return sunxi_nfc_hw_ecc_read_page_dma(nand, main_data, 1, 17);
    case 2: return sunxi_nfc_hw_ecc_read_subpage(nand, 1025, 3072, main_data, 17);
    case 3: return sunxi_nfc_hw_ecc_read_subpage_dma(nand, 1025, 3072, main_data, 17);
    case 4:
        nand->ecc.read_page = sunxi_nfc_hw_ecc_read_page;
        return sunxi_nfc_hw_ecc_read_oob(nand, 17);
    default:
        nand->ecc.read_page = sunxi_nfc_hw_ecc_read_page_dma;
        return sunxi_nfc_hw_ecc_read_oob(nand, 17);
    }
}

static void batch_transport_errors(void)
{
    unsigned cases=0;
    flow.enabled=false;
    for(unsigned variable=0;variable<2;variable++)
    for(unsigned dma=0;dma<2;dma++)
    for(unsigned random=0;random<2;random++)
    for(unsigned kind=0;kind<2;kind++)
    for(unsigned want=0;want<2;want++) {
        struct caps c=caps(variable);
        controller.caps=&c;
        struct nand_chip nand=chip(3,56,256);
        u8 user[]={32,4,0}, kinds[]={kind ? 3 : 0,0,3};
        nand.user_data_bytes=variable ? user : NULL;
        nand.options=random ? NAND_NEED_SCRAMBLING : 0;
        struct sunxi_nfc_oob_ops ops={.transform_bbm=NULL};
        nand.oob_ops=&ops;
        unsigned total=0;
        for(unsigned fail=0;fail<=total;fail++) {
            struct sunxi_nfc_ecc_read read={.buf=main_data,.page=17,.dma=dma,.oob_required=want};
            struct sunxi_nfc_ecc_snapshot snapshot;
            nand.mtd.ecc_stats.corrected=nand.mtd.ecc_stats.failed=0;
            memset(main_data,0x4b,sizeof(main_data));
            memset(oob_data,0x93,sizeof(oob_data));
            raw_calls=0; fail_raw_at=fail; raw_is_erased=true; erased_flips=1;
            load_status(0,dma ? 3 : 1,kinds,variable);
            sunxi_nfc_hw_ecc_read_status(&nand,&snapshot);
            int ret=sunxi_nfc_oob_read_batch(&nand,&read,&snapshot,0,dma ? 3 : 1);
            if(!fail) { total=raw_calls; assert(!ret); }
            else { assert(ret==-ETIMEDOUT); assert(raw_calls==fail); }
            assert(!memchr_inv(main_data+nand.mtd.writesize,0x4b,
                               sizeof(main_data)-nand.mtd.writesize));
            assert(!memchr_inv(oob_data+nand.mtd.oobsize,0x93,
                               sizeof(oob_data)-nand.mtd.oobsize));
            cases++;
        }
    }
    for(unsigned variable=0;variable<2;variable++)
    for(unsigned dma=0;dma<2;dma++) {
        struct caps c=caps(variable);
        controller.caps=&c;
        struct nand_chip nand=chip(3,56,256);
        u8 user[]={32,4,0};
        nand.user_data_bytes=variable ? user : NULL;
        unsigned total=0;
        for(unsigned fail=0;fail<=total;fail++) {
            struct sunxi_nfc_ecc_read read={.buf=main_data,.page=17,.dma=dma,.oob_required=true,
                .allwinner_oob={.corrected=5,.max_bitflips=3}};
            nand.mtd.ecc_stats.corrected=7; nand.mtd.ecc_stats.failed=2;
            memset(oob_data,0x93,sizeof(oob_data));
            raw_calls=0; fail_raw_at=fail;
            int ret=sunxi_nfc_allwinner_oob_finish_read(&nand,&read,NULL);
            if(!fail) { total=raw_calls; assert(ret==3); }
            else { assert(ret==-ETIMEDOUT); assert(raw_calls==fail); }
            assert(nand.mtd.ecc_stats.corrected==(fail ? 7 : 12));
            assert(nand.mtd.ecc_stats.failed==2);
            assert(!memchr_inv(oob_data+nand.mtd.oobsize,0x93,sizeof(oob_data)-nand.mtd.oobsize));
            cases++;
        }
    }
    fail_raw_at=0;
    printf("generic batch/vendor finish transport-failure sweep: %u\n",cases);

    /* Non-word residual OOB must remain tail space, not become an unsupported
     * user-data encoding. The first word is still reserved for the BBM. */
    struct caps c=caps(true);
    controller.caps=&c;
    for(unsigned tail=1;tail<=3;tail++) {
        struct nand_chip nand=chip(3,56,3*56+4+tail);
        assert(!sunxi_nfc_maximize_user_data(&nand,nand.mtd.oobsize,56,3));
        assert(nand.user_data_bytes[0]==4 && !nand.user_data_bytes[1] && !nand.user_data_bytes[2]);
        assert(sunxi_get_oob_offset(&nand,&nand.ecc,3)==3*56+4);
        free(nand.user_data_bytes);
    }
    /* Defensive helper checks, not geometries admitted by attach_chip(). */
    const int invalid[]={-1,1,3,33};
    for(unsigned i=0;i<ARRAY_SIZE(invalid);i++) {
        u8 saved[sizeof(controller.regs)];
        memcpy(saved,controller.regs,sizeof(saved));
        assert(sunxi_nfc_user_data_len_code(&controller,invalid[i])==-EINVAL);
        sunxi_nfc_set_user_data_len_pio(&controller,invalid[i]);
        assert(!memcmp(saved,controller.regs,sizeof(saved)));
    }
    /* A tightly packed OOB has no tail and must issue no transport at all. */
    struct nand_chip nand=chip(3,56,3*(56+4));
    int cursor=-1;
    raw_calls=0;
    assert(!sunxi_nfc_hw_ecc_write_extra_oob(&nand,oob_data,&cursor,17));
    assert(!raw_calls && cursor==-1);
}

static void control_flow(void)
{
    unsigned runs = 0;
#if FIXED_SUNXI
    /* The low-level runner checks the real selector. Here inject its errors
     * at the page-callback boundary and check that no write is started. */
    for (unsigned mdma = 0; mdma < 2; mdma++)
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned oob = 0; oob < 2; oob++)
    for (unsigned error = 0; error < 2; error++) {
        struct caps c = caps(variable);
        u8 user[] = {32, 32, 32, 32, 32, 32};
        struct nand_chip nand = chip(6, 56, 6 * (56 + (variable ? 32 : 4)) + 9);
        controller.caps = &c;
        controller.use_mdma = mdma;
        nand.user_data_bytes = variable ? user : NULL;
        nand.oob_ops = &flow_ops;
        nand.options = NAND_NEED_SCRAMBLING;
        reset_flow(0);
        select_error = error ? -EINVAL : -EBUSY;
        assert(write_case(&nand, oob ? 4 : 1, true) == select_error);
        assert(!flow.calls && !flow.prepares && !flow.begins && !flow.confirms);
        assert(!flow.mapped && !flow.active && !flow.ecc && !flow.random);
        for (unsigned i = 0; i < sizeof(controller.regs); i++)
            assert(!controller.regs[i]);
        select_error = 0;
        assert(!write_case(&nand, oob ? 4 : 1, true));
        assert(flow.confirms == 1 && !flow.mapped && !flow.active);
        assert(!flow.ecc && !flow.random);
        runs++;
    }
    printf("DMA page/OOB select failure and successful retry: %u\n", runs);
    runs = 0;
#endif
    for (unsigned mdma = 0; mdma < 2; mdma++)
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned random = 0; random < 2; random++)
    for (unsigned want = 0; want < 2; want++)
    for (unsigned operation = 0; operation < 5; operation++) {
        struct caps c = caps(variable);
        u8 user[] = {32, 32, 32, 32, 32, 32};
        struct nand_chip nand = chip(6, 56, 6 * (56 + (variable ? 32 : 4)) + 9);
        controller.caps = &c;
        controller.use_mdma = mdma;
        nand.user_data_bytes = variable ? user : NULL;
        nand.oob_ops = &flow_ops;
        nand.options = random ? NAND_NEED_SCRAMBLING : 0;
        unsigned total = 0;
        for (unsigned fail = 0; fail <= total; fail++) {
            reset_flow(fail);
            int ret = write_case(&nand, operation, want);
            if (!fail) total = flow.calls;
            assert(!flow.mapped && !flow.active && !flow.ecc && !flow.random);
            assert(flow.confirms <= 1 && flow.begins <= 1);
            if (fail) assert(flow.failure != FP_NONE);
            bool fallback = flow.failure == FP_PREPARE && !flow.issued_at_failure;
            if (!fail || fallback) {
                assert(!ret && flow.confirms == 1 && flow.begins == 1);
            } else {
                assert(ret == -ETIMEDOUT);
                assert(flow.confirms == (flow.failure == FP_CONFIRM));
            }
            assert(main_data[nand.mtd.writesize] == 0x5a);
            assert(oob_data[nand.mtd.oobsize] == 0xa5);
            runs++;
        }
    }
    printf("source-extracted write/OOB/subpage fault paths: %u\n", runs);

    runs = 0;
    for (unsigned mdma = 0; mdma < 2; mdma++)
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned operation = 0; operation < 6; operation++) {
        struct caps c = caps(variable);
        u8 user[] = {32, 32, 32, 32, 32, 32};
        struct nand_chip nand = chip(6, 56, 6 * (56 + (variable ? 32 : 4)) + 9);
        controller.caps = &c;
        controller.use_mdma = mdma;
        nand.user_data_bytes = variable ? user : NULL;
        nand.oob_ops = &flow_ops;
        nand.options = NAND_NEED_SCRAMBLING;
        bool subpage = operation == 2 || operation == 3;
        expected_stop = subpage ? 5 : 6;
        unsigned total = 0;
        for (unsigned fail = 0; fail <= total; fail++) {
            reset_flow(fail);
            nand.mtd.ecc_stats.corrected = 7;
            nand.mtd.ecc_stats.failed = 3;
            int ret = read_case(&nand, operation);
            if (!fail) total = flow.calls;
            if (fail) assert(flow.failure != FP_NONE);
            assert(!flow.mapped && !flow.active && !flow.ecc && !flow.random);
            assert(!flow.begins && !flow.confirms);
            if (ret >= 0) {
                unsigned count = expected_stop - (subpage && pio_seen ? 1 : 0);
                assert(ret == 2);
                assert(nand.mtd.ecc_stats.corrected == 7 + count);
                assert(nand.mtd.ecc_stats.failed == 3 + count);
                assert(read_seen >> expected_stop == 0);
                unsigned mask = GENMASK(expected_stop - 1, subpage && pio_seen ? 1 : 0);
                assert((read_seen & mask) == mask);
            } else {
                assert(ret == -ETIMEDOUT);
            }
            runs++;
        }
    }
    puts("DMA fallback restores both ECC counters before PIO retry");
    printf("source-extracted read/OOB/subpage fault paths: %u\n", runs);
    for(unsigned random=0;random<2;random++) {
        struct caps c=caps(false);
        controller.caps=&c;
        controller.use_mdma=false;
        struct nand_chip nand=chip(1,28,32);
        nand.mtd.writesize=nand.ecc.size=512;
        nand.options=random ? NAND_NEED_SCRAMBLING : 0;
        nand.oob_ops=&flow_ops;
        expected_stop=1;
        reset_flow(0);
        assert(!write_case(&nand,0,true));
        assert(flow.confirms==1 && !flow.ecc && !flow.random);
        reset_flow(0);
        assert(read_case(&nand,0)==2);
        assert(read_seen==1 && !flow.ecc && !flow.random);
    }
    flow.enabled=false;
    batch_transport_errors();
}
