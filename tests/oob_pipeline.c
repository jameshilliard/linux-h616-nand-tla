/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Observe unchanged batch/finish helpers using independent input vectors.
 * ECC hardware outputs and raw NAND transfers are explicit fixture inputs.
 * This is not a controller emulator or a full page-caller execution. */
static FILE *oob_trace_file;
static unsigned oob_trace_reads;

static void oob_json_bytes(const u8 *p, unsigned len)
{
    fputc('[', oob_trace_file);
    for (unsigned i = 0; i < len; i++)
        fprintf(oob_trace_file, "%s%u", i ? "," : "", p[i]);
    fputc(']', oob_trace_file);
}

static void oob_json_bits(u32 bits, unsigned len)
{
    fputc('[', oob_trace_file);
    for (unsigned i = 0; i < len; i++)
        fprintf(oob_trace_file, "%s%u", i ? "," : "", !!(bits & BIT(i)));
    fputc(']', oob_trace_file);
}

static void oob_observe_transfer(struct nand_chip *n, unsigned col, void *dst,
                                 unsigned len, int ret)
{
    assert(col >= n->mtd.writesize && dst);
    ptrdiff_t off = (u8 *)dst - n->oob_poi;
    assert(off >= 0 && off + len <= n->mtd.oobsize);
    fprintf(oob_trace_file, "%s{\"off\":%u,\"dest\":%td,\"len\":%u,\"ret\":%d}",
            oob_trace_reads++ ? "," : "", col - n->mtd.writesize, off, len, ret);
}

static void oob_pipeline(void)
{
    const char *path = getenv("NAND_OOB_VECTORS");
    if (!path) return;
    const char *output = getenv("NAND_OOB_TRACE");
    assert(output);
    FILE *vectors = fopen(path, "rb");
    oob_trace_file = fopen(output, "w");
    assert(vectors && oob_trace_file);
    u32 h[12];
    unsigned runs = 0;
    size_t got;
    while ((got = fread(h, sizeof(*h), ARRAY_SIZE(h), vectors)) == ARRAY_SIZE(h)) {
        unsigned id=h[1], variable=h[2], steps=h[3], size=h[4], parity=h[5];
        unsigned spare=h[6], page=h[7], ppb=h[8], dma=h[9], want=h[10], fail=h[11];
        assert(h[0] == 0x4f4f4254 && steps && steps <= (variable ? 32U : 16U));
        assert((size == 512 || size == 1024) && spare < 4096);
        assert(!variable || size == 1024);
        u8 kinds[32], decoded[4096], physical[40000];
        assert(fread(kinds, 1, steps, vectors) == steps);
        assert(fread(decoded, 1, spare, vectors) == spare);
        memset(physical, 0x7b, sizeof(physical));
        assert(fread(physical+steps*size, 1, spare, vectors) == spare);
        struct caps c = caps(variable);
        struct nand_chip n = chip(steps, parity, spare);
        n.ecc.size = size;
        n.mtd.writesize = steps * size;
        n.mtd.erasesize = n.mtd.writesize * ppb;
        n.mtd.ecc_step_size = size;
        n.mtd.ecc_stats.corrected = 7;
        n.mtd.ecc_stats.failed = 3;
        n.options = NAND_NEED_SCRAMBLING;
        n.oob_ops = TEST_VENDOR_OOB_OPS(variable);
        controller.caps = &c;
        flow.enabled = false;
        allocation_failure = false;
        assert(!n.oob_ops->init_user_data(&n, steps));
        memset(main_data, 0x4b, sizeof(main_data));
        memset(oob_data, 0xa5, sizeof(oob_data));
        raw_calls = fail_raw_at = 0;
        struct sunxi_nfc_ecc_read read = {
            .buf = main_data, .page = page, .dma = dma, .oob_required = want,
        };
        fprintf(oob_trace_file, "{\"id\":%u,\"user\":[", id);
        for (unsigned i=0; i<steps; i++)
            fprintf(oob_trace_file, "%s%u", i ? "," : "", sunxi_nfc_user_data_sz(&n,i));
        fputs("],\"batches\":[", oob_trace_file);
        for (unsigned first=0; first<steps;) {
            unsigned count=dma ? sunxi_nfc_dma_batch_steps(&n,first,steps) : 1;
            load_status(first, count, kinds, variable);
            unsigned reg=0;
            for (unsigned i=first; i<first+count; i++) {
                unsigned len=sunxi_nfc_user_data_sz(&n,i);
                unsigned off=sunxi_get_oob_offset(&n,&n.ecc,i);
                for (unsigned j=0; j<len; j+=4)
                    writel(sunxi_nfc_buf_to_user_data(decoded+off+j),
                           controller.regs+c.reg_user_data+(reg+j/4)*4);
                reg += len/4;
            }
            struct sunxi_nfc_ecc_snapshot snapshot;
            sunxi_nfc_hw_ecc_read_status(&n,&snapshot);
            assert(!sunxi_nfc_allwinner_oob_read_batch(&n,&read,&snapshot,first,count));
            fprintf(oob_trace_file, "%s{\"first\":%u,\"count\":%u,\"errors\":",
                    first ? "," : "", first, count);
            oob_json_bits(read.allwinner_oob.error_steps,steps);
            fputs(",\"zeros\":",oob_trace_file);
            oob_json_bits(read.allwinner_oob.zero_steps,steps);
            fprintf(oob_trace_file, ",\"corrected\":%u,\"maximum\":%u,\"global\":[%u,%u]}",
                    read.allwinner_oob.corrected,read.allwinner_oob.max_bitflips,
                    n.mtd.ecc_stats.corrected,n.mtd.ecc_stats.failed);
            first += count;
        }
        fputs("],\"captured\":",oob_trace_file);
        oob_json_bytes(oob_data,spare);
        fprintf(oob_trace_file, ",\"erased\":%s,\"reads\":[",
                sunxi_nfc_allwinner_oob_spare_is_erased(&n,page) ? "true" : "false");
        format_image=physical;
        format_image_len=n.mtd.writesize+spare;
        raw_calls=oob_trace_reads=0;
        fail_raw_at=fail;
        oob_transfer_observer=oob_observe_transfer;
        int ret=sunxi_nfc_allwinner_oob_finish_read(&n,&read,NULL);
        oob_transfer_observer=NULL;
        format_image=NULL;
        fail_raw_at=0;
        fprintf(oob_trace_file, "],\"ret\":%d,\"stats\":[%u,%u],\"main\":[%s,%s,%s],\"oob\":",
                ret,n.mtd.ecc_stats.corrected,n.mtd.ecc_stats.failed,
                memchr_inv(main_data,0x4b,n.mtd.writesize) ? "false" : "true",
                memchr_inv(main_data,0,n.mtd.writesize) ? "false" : "true",
                memchr_inv(main_data,255,n.mtd.writesize) ? "false" : "true");
        oob_json_bytes(oob_data,spare);
        bool guards=!memchr_inv(main_data+n.mtd.writesize,0x4b,sizeof(main_data)-n.mtd.writesize) &&
                    !memchr_inv(oob_data+spare,0xa5,sizeof(oob_data)-spare);
        fprintf(oob_trace_file, ",\"guards\":%s}\n", guards ? "true" : "false");
        assert(guards);
        free(n.user_data_bytes);
        runs++;
    }
    assert(!got && feof(vectors) && !ferror(vectors));
    assert(!fclose(vectors) && !fclose(oob_trace_file));
    oob_trace_file=NULL;
    printf("randomized OOB pipeline: %u source batch/classification/transport traces\n",runs);
}
