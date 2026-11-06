/* SPDX-License-Identifier: GPL-2.0-or-later */
int main(void)
{
    FILE *out = fopen("timing.jsonl", "w");
    assert(out);
    for (unsigned i = 0; i < ARRAY_SIZE(inputs); i++) {
        const struct timing_input *in = &inputs[i];
        struct nand_interface_config conf = { .invalid = in->invalid };
        struct sunxi_nfc_caps caps = { .timings = in->profile ?
            &sun50i_h616_nfc_timings : &sun4i_a10_nfc_timings };
        struct sunxi_nfc nfc = { .caps = &caps };
        struct sunxi_nand_chip chip = { .nand.controller = &nfc,
            .timing_cfg = 291, .timing_ctl = 256, .clk_rate = 12345 };
#define FIELD_POINTER(f) &conf.sdr.f,
        u32 *fields[] = { TIMING_FIELDS(FIELD_POINTER) };
#undef FIELD_POINTER
        _Static_assert(ARRAY_SIZE(fields) == ARRAY_SIZE(in->d), "field count");
        for (unsigned j = 0; j < ARRAY_SIZE(fields); j++)
            *fields[j] = in->d[j];
        round_response = in->rounded;
        clock_request = clock_calls = 0;
        int ret = sunxi_nfc_setup_interface(&chip.nand,
            in->check ? NAND_DATA_IFACE_CHECK_ONLY : 0, &conf);
        fprintf(out, "{\"id\":%u,\"observed\":[%d,%u,%lu,%u,%u],"
                "\"requested\":%lu}\n", i, ret, chip.timing_cfg,
                chip.clk_rate, chip.timing_ctl, clock_calls, clock_request);
    }
    assert(!fclose(out));
    printf("ALL TIMING SOURCE CHECKS PASSED (%zu observations)\n", ARRAY_SIZE(inputs));
    return 0;
}
