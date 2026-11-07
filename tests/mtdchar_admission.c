/* SPDX-License-Identifier: GPL-2.0-only */
int main(void)
{
	unsigned char data[1], oob[6];
	struct mtd_info mtd = {
		.size=8, .writesize=4, .erasesize=8, .oobsize=5, .oobavail=5,
		._read_oob=mtd_read_oob,
	};
	struct mtd_read_req req = {
		.start=4, .len=1, .ooblen=6, .usr_data=(uintptr_t)data,
		.usr_oob=(uintptr_t)oob, .mode=MTD_OPS_RAW,
	};
	/* GCC initializes automatic objects to a recognizable nonzero pattern.
	 * Neither the ioctl nor the real MTD wrapper is patched in this run. */
	assert(mtdchar_read_ioctl(&mtd, &req) == -EINVAL);
	assert(!backend_entries && !live_allocs);
	assert(!req.len && !req.ooblen);
#if INIT_STATS
	assert(!req.ecc_stats.uncorrectable_errors);
	assert(!req.ecc_stats.corrected_bitflips);
	assert(!req.ecc_stats.max_bitflips);
	puts("Initialized-stats candidate: rejection returns zero ECC statistics");
#else
	assert(req.ecc_stats.uncorrectable_errors == UINT32_C(0xfefefefe));
	assert(req.ecc_stats.corrected_bitflips == UINT32_C(0xfefefefe));
	assert(req.ecc_stats.max_bitflips == UINT32_C(0xfefefefe));
	puts("Existing admission bug reproduced: rejection copies poisoned ECC statistics");
#endif
	/* The first chunk is admitted. The following chunk exceeds the remaining
	 * OOB capacity and returns before the core resets ops->stats. */
	unsigned char prefix_data[20], prefix_oob[31];
	mtd.size = 24;
	mtd.erasesize = 16;
	mtd.ecc_strength = 4;
	mtd.bitflip_threshold = 1;
	allow_prefix = true;
	req = (struct mtd_read_req) {
		.len=sizeof(prefix_data), .ooblen=sizeof(prefix_oob),
		.usr_data=(uintptr_t)prefix_data, .usr_oob=(uintptr_t)prefix_oob,
		.mode=MTD_OPS_RAW,
	};
	assert(mtdchar_read_ioctl(&mtd, &req) == -EINVAL);
	assert(backend_entries == 1 && !live_allocs);
	assert(req.len > 0 && req.len < sizeof(prefix_data));
	assert(req.ooblen > 0 && req.ooblen < sizeof(prefix_oob));
#if INIT_STATS
	assert(!req.ecc_stats.uncorrectable_errors);
	assert(req.ecc_stats.max_bitflips == 3);
	assert(req.ecc_stats.corrected_bitflips == 3);
	puts("Initialized loop-local statistics preserve the admitted prefix on later rejection");
#else
	assert(req.ecc_stats.uncorrectable_errors == UINT32_C(0xfefefefe));
	assert(req.ecc_stats.max_bitflips == UINT32_C(0xfefefefe));
	assert(req.ecc_stats.corrected_bitflips == UINT32_C(0xfefefefe) + 3);
	puts("Later admission rejection also copies undefined statistics after a valid prefix");
#endif
	puts("MTDCHAR ADMISSION CHECKS PASSED");
	return 0;
}
