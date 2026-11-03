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

static int write_case(struct nand_chip *nand, unsigned operation)
{
    switch (operation) {
    case 0: return sunxi_nfc_hw_ecc_write_page(nand, main_data, 1, 17);
    case 1: return sunxi_nfc_hw_ecc_write_page_dma(nand, main_data, 1, 17);
    case 2: return sunxi_nfc_hw_ecc_write_subpage(nand, 1025, 1024, main_data, 1, 17);
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

static void control_flow(void)
{
    unsigned runs = 0;
    for (unsigned mdma = 0; mdma < 2; mdma++)
    for (unsigned variable = 0; variable < 2; variable++)
    for (unsigned operation = 0; operation < 5; operation++) {
        struct caps c = caps(variable);
        u8 user[] = {32, 32, 32, 32, 32, 32};
        struct nand_chip nand = chip(6, 56, 6 * (56 + (variable ? 32 : 4)) + 9);
        controller.caps = &c;
        controller.use_mdma = mdma;
        nand.user_data_bytes = variable ? user : NULL;
        nand.oob_ops = &flow_ops;
        nand.options = NAND_NEED_SCRAMBLING;
        unsigned total = 0;
        for (unsigned fail = 0; fail <= total; fail++) {
            reset_flow(fail);
            int ret = write_case(&nand, operation);
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
    flow.enabled = false;
    puts("DMA fallback restores both ECC counters before PIO retry");
    printf("source-extracted read/OOB/subpage fault paths: %u\n", runs);
}
