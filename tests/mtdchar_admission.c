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
	puts("MTDCHAR ADMISSION CHECKS PASSED");
	return 0;
}
