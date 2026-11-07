// SPDX-License-Identifier: GPL-2.0-only
// Exact wrapper at d48723711bae9b8cdc56719c448ba42fef28cbee
#line 1787 "mtdcore.c"
int mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	struct mtd_info *master = mtd_get_master(mtd);
	struct mtd_ecc_stats old_stats = master->ecc_stats;
	int ret_code;

	ops->retlen = ops->oobretlen = 0;

	ret_code = mtd_check_oob_ops(mtd, from, ops);
	if (ret_code)
		return ret_code;

	ledtrig_mtd_activity();

	/* Check the validity of a potential fallback on mtd->_read */
	if (!master->_read_oob && (!master->_read || ops->oobbuf))
		return -EOPNOTSUPP;

	if (ops->stats)
		memset(ops->stats, 0, sizeof(*ops->stats));

	if (mtd->flags & MTD_SLC_ON_MLC_EMULATION)
		ret_code = mtd_io_emulated_slc(mtd, from, true, ops);
	else
		ret_code = mtd_read_oob_std(mtd, from, ops);

	mtd_update_ecc_stats(mtd, master, &old_stats);

	/*
	 * In cases where ops->datbuf != NULL, mtd->_read_oob() has semantics
	 * similar to mtd->_read(), returning a non-negative integer
	 * representing max bitflips. In other cases, mtd->_read_oob() may
	 * return -EUCLEAN. In all cases, perform similar logic to mtd_read().
	 */
	if (unlikely(ret_code < 0))
		return ret_code;
	if (mtd->ecc_strength == 0)
		return 0;	/* device lacks ecc */
	if (ops->stats)
		ops->stats->max_bitflips = ret_code;
	return ret_code >= mtd->bitflip_threshold ? -EUCLEAN : 0;
}
