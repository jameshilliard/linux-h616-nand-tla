/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Observe commands and payloads, not the flash program result or BCH parity. */
static struct {
    struct nand_chip *chip;
    unsigned offset[9], user[8], first, end, next, loads;
    u8 data[8192], spare[1024];
} subwr;

static void subwr_load(struct nand_chip *n, const u8 *buf, unsigned len)
{
    unsigned s = subwr.next, off = s * n->ecc.size;

    assert(target_selected && n == subwr.chip && !flow.mapped);
    assert(s < subwr.end && subwr.loads == s - subwr.first);
    assert(column == off && buf == main_data + off && len == (unsigned)n->ecc.size);
    assert(!memcmp(buf, subwr.data + off, len));
    subwr.loads++;
}

static void subwr_issue(u32 command)
{
    struct nand_chip *n = subwr.chip;
    const struct caps *c = controller.caps;
    unsigned s = subwr.next, off = subwr.offset[s];

    assert(target_selected && s < subwr.end && subwr.loads == s - subwr.first + 1);
    assert(command == (NFC_DATA_TRANS | NFC_DATA_SWAP_METHOD | NFC_ACCESS_DIR | NFC_ECC_OP));
    assert(column == n->mtd.writesize + off);
    if (c->reg_user_data_len) {
        for (unsigned slot = 0; slot < c->max_ecc_steps; slot++) {
            u32 v = readl(controller.regs + c->reg_user_data_len + 4 * (slot / 8));
            assert(((v >> (4 * (slot % 8))) & 15) == (slot ? 0 : subwr.user[s] / 4));
        }
    }
    for (unsigned j = 0; j < subwr.user[s]; j++) {
        u32 v = readl(controller.regs + c->reg_user_data + (j & ~3U));
        assert(((v >> (8 * (j % 4))) & 255) == subwr.spare[off + j]);
    }
    subwr.next++;
}

static int unexpected_page_write(struct nand_chip *n, const u8 *buf, int oob, int page)
{
    return -EOPNOTSUPP;
}

/* steps [first,end) are subpage-aligned. phys maps each requested OOB
 * byte to its actual spare column, independently of the layout callback. */
static void subwr_request(struct nand_chip *n, unsigned first, unsigned end, unsigned mode,
                          unsigned ooboff, const unsigned *phys, unsigned count)
{
    unsigned off = first * n->ecc.size, len = (end - first) * n->ecc.size;
    u8 data[8192], spare[256];
    struct mtd_oob_ops ops = { .mode = mode, .len = len, .datbuf = data,
        .ooboffs = ooboff, .ooblen = count, .oobbuf = count ? spare : NULL };

    assert(len < sizeof(data) && count <= sizeof(spare));
    memset(subwr.data, 0xff, n->mtd.writesize);
    memset(subwr.spare, 0xff, n->mtd.oobsize);
    for (unsigned i = 0; i < len; i++)
        subwr.data[off + i] = data[i] = (u8)(i * 17 + (i >> 8) + first);
    for (unsigned i = 0; i < count; i++)
        subwr.spare[phys[i]] = spare[i] = (u8)(i * 23 + first + 7);
    /* Poison both bounce areas to catch stale bytes from a previous request. */
    memset(main_data, 0xc1, n->mtd.writesize + 1);
    memset(n->oob_poi, 0x6e, n->mtd.oobsize + 1);
    subwr.first = subwr.next = first;
    subwr.end = end;
    subwr.loads = 0;
    flow = (typeof(flow)){ .enabled = true };
    assert(!nand_do_write_ops(n, 17 * n->mtd.writesize + off, &ops));
    assert(ops.retlen == len && ops.oobretlen == count && !target_selected);
    assert(flow.begins == 1 && flow.confirms == 1 && !flow.dma_issued);
    assert(subwr.next == end && subwr.loads == end - first);
    assert(!memcmp(main_data, subwr.data, n->mtd.writesize));
    assert(!memcmp(n->oob_poi, subwr.spare, n->mtd.oobsize));
    assert(main_data[n->mtd.writesize] == 0xc1 && n->oob_poi[n->mtd.oobsize] == 0x6e);
}

static void subpage_write_cases(void)
{
    static const struct mtd_ooblayout_ops layout = { .free = sunxi_nand_ooblayout_free };
    unsigned none = 0, aut = 0, place = 0, unaligned = 0;
    command_issue_observer = subwr_issue;
    write_buffer_observer = subwr_load;
    for (unsigned backend = 0; backend < 3; backend++) {
        struct nand_chip n = setup(1024, 8);
        struct caps c = caps;
        unsigned free_phys[256], free_base[9], nr = 0;
        unsigned tail = transfer_geometry(&n, &c, backend, 0, subwr.user);
        n.mtd.oobsize = tail + 13;
        n.mtd.ooblayout = &layout;
        n.subpagesize = n.mtd.writesize / 4; /* core SLC policy for eight ECC steps */
        n.chip_shift = 27;
        n.ecc.write_subpage = sunxi_nfc_hw_ecc_write_subpage;
        n.ecc.write_page = n.ecc.write_page_raw = unexpected_page_write;
        subwr.chip = &n;
        subwr.offset[0] = 0;
        for (unsigned s = 0; s < 8; s++) {
            free_base[s] = nr;
            for (unsigned j = s ? 0 : 2; j < subwr.user[s]; j++)
                free_phys[nr++] = subwr.offset[s] + j;
            subwr.offset[s + 1] = subwr.offset[s] + subwr.user[s] + n.ecc.bytes;
        }
        n.mtd.oobavail = free_base[8] = nr;
        assert(subwr.offset[8] == tail);
        for (unsigned a = 0; a < 8; a += 2)
        for (unsigned b = a + 2; b <= 8; b += 2) {
            if (!a && b == 8) continue; /* full-page callback tested elsewhere */
            subwr_request(&n, a, b, MTD_OPS_PLACE_OOB, 0, NULL, 0);
            none++;
            unsigned nfree = free_base[b] - free_base[a];
            if (!nfree) continue; /* ECC steps may have no free bytes */
            /* Cross region boundaries and exercise nonzero logical offsets. */
            for (unsigned skip = 0; skip <= 1 && skip < nfree; skip++) {
                unsigned start = free_base[a] + skip;
                subwr_request(&n, a, b, MTD_OPS_AUTO_OOB, start, free_phys + start, nfree - skip);
                aut++;
            }
            /* PLACE uses physical offsets. Stay within each protected region. */
            for (unsigned s = a; s < b; s++) {
                unsigned start = free_base[s], length = free_base[s + 1] - start;
                if (!length) continue;
                subwr_request(&n, a, b, MTD_OPS_PLACE_OOB, free_phys[start], free_phys + start, length);
                place++;
            }
        }
        for (unsigned bad_off = 0; bad_off <= 1; bad_off++) {
            struct mtd_oob_ops ops = { .len = n.subpagesize - !bad_off, .datbuf = main_data };
            flow = (typeof(flow)){ .enabled = true };
            assert(nand_do_write_ops(&n, bad_off, &ops) == -EINVAL);
            assert(!flow.calls && !ops.retlen && !target_selected);
            unaligned++;
        }
        controller.caps = &caps;
    }
    command_issue_observer = NULL;
    write_buffer_observer = NULL;
    flow = (typeof(flow)){0};
    printf("NAND core to subpage PIO: %u data-only, %u AUTO_OOB, %u PLACE_OOB, %u unaligned\n",
           none, aut, place, unaligned);
}
