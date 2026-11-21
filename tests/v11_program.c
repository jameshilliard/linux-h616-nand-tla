/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Program ordering, not array durability: MMIO, DMA and program-status
 * delivery are the fixture boundaries. No NAND is written by these tests. */
static unsigned tail_offset, tail_writes;
static unsigned next_step, end_step, protected[8];
static struct nand_chip *program_chip;

static u8 spare_byte(unsigned i) { return (u8)(0x25 + 19 * i + (i >> 4)); }

/* Observe the words the controller would consume when the driver issues
 * the command. Derive offsets from the test geometry, not driver helpers. */
static void observe_program_issue(u32 command)
{
    struct nand_chip *n = program_chip;
    struct caps *c = controller.caps;
    unsigned first = next_step, count = 1, offset = 0, index = 0;
    if (flow.mapped && !first)
        count = c->reg_user_data_len && !n->allwinner_oob ? 4 : n->ecc.steps;
    assert(first + count <= end_step);
    for (unsigned i = 0; i < first; i++) offset += n->ecc.bytes + protected[i];
    if (flow.mapped && first) {
        assert(column == first * n->ecc.size);
        assert(readl(controller.regs + c->reg_spare_area) == n->mtd.writesize + offset);
    }
    if (c->reg_user_data_len) {
        for (unsigned slot = 0; slot < c->max_ecc_steps; slot++) {
            u32 codes = readl(controller.regs + c->reg_user_data_len + 4 * (slot / 8));
            unsigned length = slot < count ? protected[first + slot] : 0;
            assert(((codes >> (4 * (slot % 8))) & 15) == length / 4);
        }
    }
    for (unsigned i = first; i < first + count; i++) {
        for (unsigned j = 0; j < protected[i]; j += 4) {
            u32 word = readl(controller.regs + c->reg_user_data + 4 * index++);
            for (unsigned b = 0; b < 4; b++) {
                /* The independent marker suite checks this compensation. */
                if (!i && j + b < 2 && n->options && !n->allwinner_oob)
                    continue;
                assert(((word >> (8 * b)) & 255) == spare_byte(offset + j + b));
            }
        }
        offset += n->ecc.bytes + protected[i];
    }
    next_step += count;
}

static void observe_program_buffer(struct nand_chip *n, const u8 *buf, unsigned len)
{
    uintptr_t addr = (uintptr_t)buf, spare = (uintptr_t)n->oob_poi;

    assert(!flow.confirms);
    if (addr >= spare && addr - spare < n->mtd.oobsize) {
        assert(addr - spare == tail_offset && len == n->mtd.oobsize - tail_offset);
        tail_writes++;
    }
}

static unsigned program_trial(struct nand_chip *n, unsigned op, bool extra,
                              unsigned batches, unsigned fail, unsigned again,
                              bool fail_abort)
{
    bool use_dma = op == 1 || op == 4, oob_only = op >= 3;
    flow = (typeof(flow)){ .enabled = true, .fail_at = fail, .fail_again_at = again,
                           .abort_failure = fail_abort, .prepare_errno = -ENOMEM };
    program_chip = n;
    next_step = op == 2 ? 2 : 0;
    end_step = op == 2 ? 4 : n->ecc.steps;
    tail_writes = column = 0;
    memset(main_data, 0x5a, n->mtd.writesize + 1);
    for (unsigned i = 0; i <= n->mtd.oobsize; i++) n->oob_poi[i] = spare_byte(i);
    int ret = write_request(n, op, extra, 2048, 2048);
    bool retry = flow.failure == FP_PREPARE && !flow.issued_at_failure;
    assert(!flow.mapped && !flow.active && !flow.ecc && !flow.random);
    assert(!fail || flow.failure != FP_NONE);
    if (!fail || retry) {
        bool need_tail = tail_offset < n->mtd.oobsize && op != 2 &&
                         (extra || oob_only || (n->options & NAND_NEED_SCRAMBLING));
        assert(!again && ret == 0 && flow.confirms == 1 && flow.begins == 1);
        assert(next_step == end_step);
        assert(tail_writes == need_tail);
        unsigned dma_commands = use_dma && !retry ? batches : 0;
        assert(flow.dma_issued == dma_commands);
        assert(flow.issued == (dma_commands ? dma_commands : op == 2 ? 2 : n->ecc.steps));
    } else {
        int expected_error = flow.failure == FP_PREPARE ? -ENOMEM : -ETIMEDOUT;
        if (fail_abort && flow.aborts)
            expected_error = -EIO;
        assert(ret == expected_error);
        assert(flow.confirms == (flow.failure == FP_CONFIRM));
        assert(flow.calls == (again ? again : fail));
    }
    assert(main_data[n->mtd.writesize] == 0x5a);
    for (unsigned i = 0; i <= n->mtd.oobsize; i++) assert(n->oob_poi[i] == spare_byte(i));
    assert(!memchr_inv(main_data, oob_only ? 0xff : 0x5a, n->mtd.writesize));
    return flow.calls;
}

static void program_cases(void)
{
    unsigned cases = 0, retries = 0, closed = 0, select_errors = 0, small = 0;
    command_issue_observer = observe_program_issue;
    write_buffer_observer = observe_program_buffer;
    for (unsigned backend = 0; backend < 3; backend++)
    for (unsigned profile = 0; profile < (HAS_FW ? 3U : 2U); profile++) {
        /* A10 uses external DMA; A23 has internal DMA but fixed user words.
         * H616's ordinary 32-byte layout spans a 128-byte register bank. */
        bool variable = backend == 2, fw = profile == 2;
        struct nand_chip n = setup(1024, 8);
        struct caps c = caps;
        tail_offset = transfer_geometry(&n, &c, backend, profile, protected);
        n.mtd.oobsize = tail_offset + (variable && !fw ? 3 : 16);
        unsigned batches = variable && !fw ? 5 : 1;

        for (unsigned op = 0; op < 5; op++)
        for (unsigned extra = 0; extra < 2; extra++) {
            if ((op == 2 && profile) || (op >= 3 && !extra))
                continue; /* Scrambled chips disallow subpage programs. */
            if (op == 1 || op == 4) {
                flow = (typeof(flow)){ .enabled = true };
                select_error = -EBUSY;
                assert(write_request(&n, op, extra, 2048, 2048) == -EBUSY);
                assert(!flow.calls && !flow.prepares && !flow.issued && !flow.confirms);
                select_error = 0;
                select_errors++;
            }
            unsigned normal = program_trial(&n, op, extra, batches, 0, 0, false);
            cases++;
            for (unsigned fail = 1; fail <= normal; fail++) {
                unsigned last = program_trial(&n, op, extra, batches, fail, 0, false);
                cases++;
                if (flow.aborts && variable) {
                    program_trial(&n, op, extra, batches, fail, 0, true);
                    assert(flow.reset_held);
                    closed++;
                } else if (last > fail && flow.failure == FP_PREPARE) {
                    /* First prepare failed with no data issued. Fault every
                     * subsequent step in the actual PIO retry, too. */
                    for (unsigned second = fail + 1; second <= last; second++) {
                        program_trial(&n, op, extra, batches, fail, second, false);
                        assert(flow.prepares == 1 && !flow.dma_issued);
                        retries++;
                    }
                }
            }
        }
        controller.caps = &caps;
    }
    /* Command-only control, not evidence that small-page x8 media pass
     * controller-ECC admission. The NAND core rejects RNDIN on small pages. */
    for (unsigned tail = 0; tail <= 4; tail += 4)
    for (unsigned random = 0; random < 2; random++)
    for (unsigned op = 0; op <= 3; op += 3)
    for (unsigned extra = (op == 3); extra < 2; extra++) {
        struct nand_chip n = setup(512, 1);
        struct caps c = caps;
        c.reg_user_data_len = 0;
        controller.caps = &c;
        n.options = random ? NAND_NEED_SCRAMBLING : 0;
        protected[0] = 4;
        tail_offset = n.ecc.bytes + 4;
        n.mtd.oobsize = tail_offset + tail;
        program_trial(&n, op, extra, 1, 0, 0, false);
        controller.caps = &caps;
        small++;
    }
    command_issue_observer = NULL;
    write_buffer_observer = NULL;
    flow = (typeof(flow)){0};
    printf("write/OOB/subpage: %u single-fault/control, %u PIO retry faults, "
           "%u failed aborts, %u chip-select failures, %u small-page commands\n",
           cases, retries, closed, select_errors, small);
}
