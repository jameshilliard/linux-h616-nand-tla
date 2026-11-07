/* SPDX-License-Identifier: GPL-2.0-only */
/* Admission rejection must not enter this backend. One admitted prefix may
 * precede the rejection to check statistics from later iterations as well. */
static unsigned int backend_entries;
static bool allow_prefix;
static void ledtrig_mtd_activity(void) {}
static void mtd_update_ecc_stats(struct mtd_info *mtd, struct mtd_info *master,
			       struct mtd_ecc_stats *old) {}
static int mtd_io_emulated_slc(struct mtd_info *mtd, loff_t from,
			       bool read, struct mtd_oob_ops *ops)
{
	backend_entries++;
	return -EIO;
}
static int mtd_read_oob_std(struct mtd_info *mtd, loff_t from,
			  struct mtd_oob_ops *ops)
{
	backend_entries++;
	assert(allow_prefix && backend_entries == 1);
	memset(ops->datbuf, 0xa5, ops->len);
	memset(ops->oobbuf, 0x5a, ops->ooblen);
	ops->retlen = ops->len;
	ops->oobretlen = ops->ooblen;
	ops->stats->corrected_bitflips = 3;
	return 3;
}
#define unlikely(x) (x)
