// SPDX-License-Identifier: GPL-2.0-only
// Exact function body extracted from d48723711bae9b8cdc56719c448ba42fef28cbee
#line 1659 "mtdcore.c"
static int mtd_check_oob_ops(struct mtd_info *mtd, loff_t offs,
			     struct mtd_oob_ops *ops)
{
	/*
	 * Some users are setting ->datbuf or ->oobbuf to NULL, but are leaving
	 * ->len or ->ooblen uninitialized. Force ->len and ->ooblen to 0 in
	 *  this case.
	 */
	if (!ops->datbuf)
		ops->len = 0;

	if (!ops->oobbuf)
		ops->ooblen = 0;

	if (offs < 0 || offs + ops->len > mtd->size)
		return -EINVAL;

	if (ops->ooblen) {
		size_t maxooblen;

		if (ops->ooboffs >= mtd_oobavail(mtd, ops))
			return -EINVAL;

		maxooblen = ((size_t)(mtd_div_by_ws(mtd->size, mtd) -
				      mtd_div_by_ws(offs, mtd)) *
			     mtd_oobavail(mtd, ops)) - ops->ooboffs;
		if (ops->ooblen > maxooblen)
			return -EINVAL;
	}

	return 0;
}
