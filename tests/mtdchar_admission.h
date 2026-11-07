/* SPDX-License-Identifier: GPL-2.0-only */
/* Only the admission path is exercised. Entering a backend is a test failure. */
static unsigned int backend_entries;
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
	return -EIO;
}
#define unlikely(x) (x)
