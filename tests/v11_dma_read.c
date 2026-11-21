/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real v11 page/subpage, batch and ECC code; the NAND image, DMA completion
 * and status registers are supplied by the fixture. No physical flash I/O. */
static struct {
    struct nand_chip *chip;
    unsigned first, stop, next, device_column, pio_step, pio_seen, lengths[8], media_errors;
    bool retry_seen, fail_third_pio;
} rd;
enum { old_corrected = 11, old_failed = 7 };

static unsigned read_spare_offset(unsigned step)
{
    unsigned off = step * rd.chip->ecc.bytes;
    for (unsigned i = 0; i < step; i++) off += rd.lengths[i];
    return off;
}

/* Different correctable counts on DMA and PIO expose stale totals or a
 * stale maximum. A media failure is not a transport failure. */
static unsigned flips(unsigned step, bool via_dma) { return step + (via_dma ? 9 : 1); }

static void observe_read_register(unsigned offset)
{
    struct caps *c = controller.caps;
    if (offset == NFC_REG_ECC_ST || offset == c->reg_pat_found || offset == c->reg_pat_id ||
        (offset >= c->reg_ecc_err_cnt && offset < c->reg_ecc_err_cnt + 8))
        assert(!flow.active);
}

/* Publish device writes to the CPU view only at unmap. An aborted
 * transfer does not promise completed contents. */
static void return_read_buffer(const void *buf, unsigned len, int dir)
{
    assert(dir == DMA_FROM_DEVICE && !flow.active);
    if (!flow.aborts) memcpy((u8 *)buf, image + rd.device_column, len);
}

static void observe_read_transfer(struct nand_chip *n, unsigned col, void *dst,
                                  unsigned len, int ret)
{
    if (flow.page_attempts == 2 && !rd.retry_seen) {
        assert(!col && !dst && !len);
        assert(n->mtd.ecc_stats.corrected == old_corrected);
        assert(n->mtd.ecc_stats.failed == old_failed);
        rd.retry_seen = true;
    }
    if (ret) return;
    if (!len && !dst)
        writel(n->mtd.writesize, controller.regs + controller.caps->reg_spare_area);
    if (!dst && len == n->ecc.size && col < n->mtd.writesize) {
        rd.pio_step = col / len;
        assert(rd.retry_seen && col % len == 0 && rd.pio_step >= rd.first && rd.pio_step < rd.stop);
        assert(!(rd.pio_seen & BIT(rd.pio_step)));
        rd.pio_seen |= BIT(rd.pio_step);
        memcpy(controller.regs + NFC_RAM0_BASE, image + col, len);
        if (rd.fail_third_pio && rd.pio_step == rd.first + 2)
            flow.fail_again_at = flow.calls + 1;
    }
}

static void observe_read_issue(u32 command)
{
    struct nand_chip *n = rd.chip;
    struct caps *c = controller.caps;
    bool via_dma = flow.mapped;
    unsigned first = via_dma ? rd.next : rd.pio_step, count = 1, errors = 0;
    unsigned off = read_spare_offset(first), pos = 0;
    assert(first < rd.stop);
    if (via_dma) {
        /* The first transfer may fill the bank, later ones restart at slot zero. */
        if (!first) count = c->reg_user_data_len && !n->allwinner_oob ? min(4U, rd.stop) : rd.stop;
        assert(command == (NFC_PAGE_OP | NFC_DATA_SWAP_METHOD | NFC_DATA_TRANS));
        assert(flow.mapped_direction == DMA_FROM_DEVICE);
        assert(flow.mapped_bytes == count * n->ecc.size);
        assert(flow.mapped_buf == main_data + first * n->ecc.size);
        assert(column == first * n->ecc.size);
        assert(readl(controller.regs + c->reg_spare_area) == n->mtd.writesize + off);
        rd.device_column = column;
        rd.next += count;
    } else {
        assert(command == (NFC_ECC_OP | NFC_DATA_SWAP_METHOD | NFC_DATA_TRANS));
        assert(column == n->mtd.writesize + off);
        column += rd.lengths[first] + n->ecc.bytes;
    }
    if (c->reg_user_data_len) {
        for (unsigned i = 0; i < c->max_ecc_steps; i++) {
            u32 codes = readl(controller.regs + c->reg_user_data_len + (i / 8) * 4);
            assert(((codes >> (4 * (i % 8))) & 15) == (i < count ? rd.lengths[first + i] / 4 : 0));
        }
    }
    for (unsigned i = 0; i < count; i++) {
        unsigned s = first + i;
        if (rd.media_errors & BIT(s)) errors |= BIT(i);
        controller.regs[c->reg_ecc_err_cnt + i] = flips(s, via_dma);
        memset(controller.regs + c->reg_user_data + pos, 0x35 + s, rd.lengths[s]);
        pos += rd.lengths[s];
    }
    writel(errors, controller.regs + NFC_REG_ECC_ST);
    if (c->reg_pat_found != NFC_REG_ECC_ST) writel(0, controller.regs + c->reg_pat_found);
}

enum read_retry_fault { NO_RETRY_FAULT, PIO_BEGIN, THIRD_PIO_STEP };
static unsigned read_trial(struct nand_chip *n, unsigned first, unsigned end, bool sub,
                           bool oob, unsigned fail, enum read_retry_fault again, bool abort_fails)
{
    flow = (typeof(flow)){ .enabled = true, .fail_at = fail, .fail_again_at = again == PIO_BEGIN ? fail + 1 : 0,
                           .abort_failure = abort_fails, .prepare_errno = -ENOMEM };
    controller.recovery_failed = false;
    rd.chip = n;
    rd.first = n->allwinner_oob ? 0 : first;
    rd.stop = n->allwinner_oob ? n->ecc.steps : end;
    rd.next = rd.pio_seen = 0;
    rd.retry_seen = false;
    rd.fail_third_pio = again == THIRD_PIO_STEP;
    n->mtd.ecc_stats.corrected = old_corrected;
    n->mtd.ecc_stats.failed = old_failed;
    memset(main_data, 0x65, n->mtd.writesize + 1);
    memset(n->oob_poi, 0xca, n->mtd.oobsize + 1);
    int ret = sub ? n->ecc.read_subpage(n, first * 1024 + 3, (end - first) * 1024 - 8, main_data, 0) :
                    n->ecc.read_page(n, main_data, oob, 0);
    bool closed = abort_fails && flow.aborts;
    bool retry = fail > 1 && !closed;
    assert(flow.page_attempts == 1U + retry);
    assert(rd.retry_seen == retry && controller.recovery_failed == closed);
    assert(!flow.mapped && !flow.active && !flow.ecc && !flow.random);
    assert(flow.pending_issues == (controller.use_mdma ? 0 : flow.dma_issued));
    if (flow.dma_issued)
        assert(flow.dma_wait_events == (NFC_CMD_INT_FLAG | (controller.use_mdma ? NFC_DMA_INT_FLAG : 0)));
    assert(main_data[n->mtd.writesize] == 0x65 && n->oob_poi[n->mtd.oobsize] == 0xca);
    if (again == THIRD_PIO_STEP) {
        unsigned mask = BIT(rd.first + 2) - BIT(rd.first);
        unsigned corrected = 0, failed = 0;
        /* Ordinary PIO has accounted for two steps; firmware classification
         * publishes its totals only when the full page can be classified. */
        for (unsigned i = rd.first; !n->allwinner_oob && i < rd.first + 2; i++) {
            if (rd.media_errors & BIT(i)) failed++;
            else corrected += flips(i, false);
        }
        assert(ret == -ETIMEDOUT && flow.calls == flow.fail_again_at);
        assert(rd.pio_seen == (mask | BIT(rd.first + 2)));
        assert(n->mtd.ecc_stats.corrected == old_corrected + corrected);
        assert(n->mtd.ecc_stats.failed == old_failed + failed);
    } else if (closed || fail == 1 || again == PIO_BEGIN) {
        assert(ret == (closed ? -EIO : -ETIMEDOUT) && !rd.pio_seen);
        assert(n->mtd.ecc_stats.corrected == old_corrected && n->mtd.ecc_stats.failed == old_failed);
        assert(flow.calls == (again == PIO_BEGIN ? fail + 1 : fail));
    } else {
        unsigned corrected = 0, failed = 0, peak = 0;
        for (unsigned s = rd.first; s < rd.stop; s++) {
            if (rd.media_errors & BIT(s)) failed++;
            else { unsigned count = flips(s, !retry); corrected += count; peak = max(peak, count); }
            assert(!memcmp(main_data + s * 1024, image + s * 1024, 1024));
        }
        assert(n->mtd.ecc_stats.corrected == old_corrected + corrected);
        assert(n->mtd.ecc_stats.failed == old_failed + failed);
        assert(ret == (int)peak);
        assert(rd.pio_seen == (retry ? BIT(rd.stop) - BIT(rd.first) : 0));
        if (!retry) assert(rd.next == rd.stop);
    }
    return flow.calls;
}

static void dma_read_cases(void)
{
    static const unsigned ranges[][2] = {{0, 8}, {2, 4}, {5, 7}, {1, 6}, {6, 7}};
    unsigned singles = 0, closed = 0, doubles = 0;
    for (unsigned backend = 0; backend < 3; backend++)
    for (unsigned profile = 0; profile < (HAS_FW ? 3U : 2U); profile++) {
        bool variable = backend == 2;
        struct nand_chip n = setup(1024, 8);
        struct caps c = caps;
        n.mtd.oobsize = transfer_geometry(&n, &c, backend, profile, rd.lengths) + 13;
        n.ecc.read_page = sunxi_nfc_hw_ecc_read_page_dma;
        n.ecc.read_subpage = sunxi_nfc_hw_ecc_read_subpage_dma;
#if HAS_FW
        if (n.allwinner_oob)
            n.ecc.read_subpage = sunxi_nfc_allwinner_oob_read_subpage;
#endif
        rd.chip = &n;
        format_image_len = n.mtd.writesize + n.mtd.oobsize;
        for (unsigned i = 0; i < format_image_len; i++) image[i] = (u8)(i * 17 + (i >> 10));
        oob_transfer_observer = observe_read_transfer;
        command_issue_observer = observe_read_issue;
        register_read_observer = observe_read_register;
        dma_unmap_observer = return_read_buffer;
        for (unsigned r = 0; r < ARRAY_SIZE(ranges); r++)
        for (unsigned oob = 0; oob <= !r; oob++)
        for (unsigned errors = 0; errors < 2; errors++) {
            unsigned first = ranges[r][0], end = ranges[r][1];
            rd.media_errors = errors ? BIT(1) | BIT(6) : 0;
            unsigned events = read_trial(&n, first, end, !!r, oob, 0, NO_RETRY_FAULT, false);
            singles++;
            for (unsigned f = 1; f <= events; f++) {
                read_trial(&n, first, end, !!r, oob, f, NO_RETRY_FAULT, false);
                singles++;
                if (variable && flow.aborts) {
                    read_trial(&n, first, end, !!r, oob, f, NO_RETRY_FAULT, true);
                    closed++;
                }
                if (f != 1) {
                    read_trial(&n, first, end, !!r, oob, f, PIO_BEGIN, false);
                    doubles++;
                    if (rd.stop - rd.first >= 3) {
                        read_trial(&n, first, end, !!r, oob, f, THIRD_PIO_STEP, false);
                        doubles++;
                    }
                }
            }
        }
        controller.caps = &caps;
    }
    oob_transfer_observer = NULL;
    command_issue_observer = NULL;
    register_read_observer = NULL;
    dma_unmap_observer = NULL;
    flow = (typeof(flow)){0};
    printf("DMA page/subpage reads: %u single-fault/control, %u failed aborts, %u PIO retry faults\n",
           singles, closed, doubles);
}
