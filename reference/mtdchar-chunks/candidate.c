// SPDX-License-Identifier: GPL-2.0-only
// Exact function bodies extracted from 51e1737afbf8d2e6902814add5848c1522f36973
#line 576 "mtdchar.c"
static void adjust_oob_length(struct mtd_info *mtd, uint64_t start,
			      struct mtd_oob_ops *ops)
{
	u64 pages;
	u32 oob_per_page;

	if (ops->len == 0 || ops->ooblen == 0)
		return;

	pages = mtd_div_by_ws(mtd_mod_by_ws(start, mtd) + (u64)ops->len - 1,
			      mtd) + 1;
	oob_per_page = mtd_oobavail(mtd, ops);

	ops->ooblen = min_t(u64, ops->ooblen, pages * oob_per_page);
}
#line 592 "mtdchar.c"
static int mtdchar_adjust_io_lengths(struct mtd_info *mtd, uint64_t start,
				     struct mtd_oob_ops *ops, uint64_t ooblen)
{
	u32 oob_per_page = mtd_oobavail(mtd, ops);
	u64 len;

	if (!ops->ooblen)
		return 0;
	if (!oob_per_page)
		return -EINVAL;

	if (ops->ooblen < ooblen) {
		/* Keep a page's OOB together when another chunk follows. */
		ops->ooblen -= ops->ooblen % oob_per_page;
		if (ops->len) {
			len = (u64)(ops->ooblen / oob_per_page) * mtd->writesize;
			len -= mtd_mod_by_ws(start, mtd);
			ops->len = min_t(u64, ops->len, len);
		}
	}

	return 0;
}
#line 616 "mtdchar.c"
static noinline_for_stack int
mtdchar_write_ioctl(struct mtd_info *mtd, struct mtd_write_req __user *argp)
{
	struct mtd_info *master = mtd_get_master(mtd);
	struct mtd_write_req req;
	const void __user *usr_data, *usr_oob;
	uint8_t *datbuf = NULL, *oobbuf = NULL;
	size_t datbuf_len, oobbuf_len;
	int ret = 0;
	u64 end;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	usr_data = (const void __user *)(uintptr_t)req.usr_data;
	usr_oob = (const void __user *)(uintptr_t)req.usr_oob;

	if (!master->_write_oob)
		return -EOPNOTSUPP;

	if (!usr_data)
		req.len = 0;

	if (!usr_oob)
		req.ooblen = 0;

	req.len &= 0xffffffff;
	req.ooblen &= 0xffffffff;

	if (check_add_overflow(req.start, req.len, &end) || end > mtd->size)
		return -EINVAL;

	datbuf_len = min_t(size_t, req.len, mtd->erasesize);
	if (datbuf_len > 0) {
		datbuf = kvmalloc(datbuf_len, GFP_KERNEL);
		if (!datbuf)
			return -ENOMEM;
	}

	oobbuf_len = min_t(size_t, req.ooblen,
			   max(mtd->erasesize, mtd->oobsize));
	if (oobbuf_len > 0) {
		oobbuf = kvmalloc(oobbuf_len, GFP_KERNEL);
		if (!oobbuf) {
			kvfree(datbuf);
			return -ENOMEM;
		}
	}

	while (req.len > 0 || (!usr_data && req.ooblen > 0)) {
		struct mtd_oob_ops ops = {
			.mode = req.mode,
			.len = min_t(size_t, req.len, datbuf_len),
			.ooblen = min_t(size_t, req.ooblen, oobbuf_len),
			.datbuf = datbuf,
			.oobbuf = oobbuf,
		};

		/*
		 * Shorten non-page-aligned, eraseblock-sized writes so that
		 * the write ends on an eraseblock boundary.  This is necessary
		 * for adjust_oob_length() to properly handle non-page-aligned
		 * writes.
		 */
		if (ops.len == mtd->erasesize)
			ops.len -= mtd_mod_by_ws(req.start + ops.len, mtd);

		ret = mtdchar_adjust_io_lengths(mtd, req.start, &ops, req.ooblen);
		if (ret)
			break;

		/*
		 * For writes which are not OOB-only, adjust the amount of OOB
		 * data written according to the number of data pages written.
		 * This is necessary to prevent OOB data from being skipped
		 * over in data+OOB writes requiring multiple mtd_write_oob()
		 * calls to be completed.
		 */
		adjust_oob_length(mtd, req.start, &ops);

		if (copy_from_user(datbuf, usr_data, ops.len) ||
		    copy_from_user(oobbuf, usr_oob, ops.ooblen)) {
			ret = -EFAULT;
			break;
		}

		ret = mtd_write_oob(mtd, req.start, &ops);
		if (ret)
			break;

		if (usr_data)
			req.start += ops.retlen;
		else
			req.start += (u64)(ops.oobretlen / mtd_oobavail(mtd, &ops)) *
				     mtd->writesize;
		req.len -= ops.retlen;
		usr_data += ops.retlen;

		req.ooblen -= ops.oobretlen;
		usr_oob += ops.oobretlen;
	}

	kvfree(datbuf);
	kvfree(oobbuf);

	return ret;
}
#line 724 "mtdchar.c"
static noinline_for_stack int
mtdchar_read_ioctl(struct mtd_info *mtd, struct mtd_read_req __user *argp)
{
	struct mtd_info *master = mtd_get_master(mtd);
	struct mtd_read_req req;
	void __user *usr_data, *usr_oob;
	uint8_t *datbuf = NULL, *oobbuf = NULL;
	size_t datbuf_len, oobbuf_len;
	size_t orig_len, orig_ooblen;
	int ret = 0;
	u64 end;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	orig_len = req.len;
	orig_ooblen = req.ooblen;

	usr_data = (void __user *)(uintptr_t)req.usr_data;
	usr_oob = (void __user *)(uintptr_t)req.usr_oob;

	if (!master->_read_oob)
		return -EOPNOTSUPP;

	if (!usr_data)
		req.len = 0;

	if (!usr_oob)
		req.ooblen = 0;

	req.ecc_stats.uncorrectable_errors = 0;
	req.ecc_stats.corrected_bitflips = 0;
	req.ecc_stats.max_bitflips = 0;

	req.len &= 0xffffffff;
	req.ooblen &= 0xffffffff;

	if (check_add_overflow(req.start, req.len, &end) || end > mtd->size) {
		ret = -EINVAL;
		goto out;
	}

	datbuf_len = min_t(size_t, req.len, mtd->erasesize);
	if (datbuf_len > 0) {
		datbuf = kvmalloc(datbuf_len, GFP_KERNEL);
		if (!datbuf) {
			ret = -ENOMEM;
			goto out;
		}
	}

	oobbuf_len = min_t(size_t, req.ooblen,
			   max(mtd->erasesize, mtd->oobsize));
	if (oobbuf_len > 0) {
		oobbuf = kvmalloc(oobbuf_len, GFP_KERNEL);
		if (!oobbuf) {
			ret = -ENOMEM;
			goto out;
		}
	}

	while (req.len > 0 || (!usr_data && req.ooblen > 0)) {
		struct mtd_req_stats stats;
		struct mtd_oob_ops ops = {
			.mode = req.mode,
			.len = min_t(size_t, req.len, datbuf_len),
			.ooblen = min_t(size_t, req.ooblen, oobbuf_len),
			.datbuf = datbuf,
			.oobbuf = oobbuf,
			.stats = &stats,
		};

		/*
		 * Shorten non-page-aligned, eraseblock-sized reads so that the
		 * read ends on an eraseblock boundary.  This is necessary in
		 * order to prevent OOB data for some pages from being
		 * duplicated in the output of non-page-aligned reads requiring
		 * multiple mtd_read_oob() calls to be completed.
		 */
		if (ops.len == mtd->erasesize)
			ops.len -= mtd_mod_by_ws(req.start + ops.len, mtd);

		ret = mtdchar_adjust_io_lengths(mtd, req.start, &ops, req.ooblen);
		if (ret)
			break;
		if (ops.len < req.len)
			adjust_oob_length(mtd, req.start, &ops);

		ret = mtd_read_oob(mtd, (loff_t)req.start, &ops);

		req.ecc_stats.uncorrectable_errors +=
			stats.uncorrectable_errors;
		req.ecc_stats.corrected_bitflips += stats.corrected_bitflips;
		req.ecc_stats.max_bitflips =
			max(req.ecc_stats.max_bitflips, stats.max_bitflips);

		if (ret && !mtd_is_bitflip_or_eccerr(ret))
			break;

		if (copy_to_user(usr_data, ops.datbuf, ops.retlen) ||
		    copy_to_user(usr_oob, ops.oobbuf, ops.oobretlen)) {
			ret = -EFAULT;
			break;
		}

		if (usr_data)
			req.start += ops.retlen;
		else
			req.start += (u64)(ops.oobretlen / mtd_oobavail(mtd, &ops)) *
				     mtd->writesize;
		req.len -= ops.retlen;
		usr_data += ops.retlen;

		req.ooblen -= ops.oobretlen;
		usr_oob += ops.oobretlen;
	}

	/*
	 * As multiple iterations of the above loop (and therefore multiple
	 * mtd_read_oob() calls) may be necessary to complete the read request,
	 * adjust the final return code to ensure it accounts for all detected
	 * ECC errors.
	 */
	if (!ret || mtd_is_bitflip(ret)) {
		if (req.ecc_stats.uncorrectable_errors > 0)
			ret = -EBADMSG;
		else if (req.ecc_stats.corrected_bitflips > 0)
			ret = -EUCLEAN;
	}

out:
	req.len = orig_len - req.len;
	req.ooblen = orig_ooblen - req.ooblen;

	if (copy_to_user(argp, &req, sizeof(req)))
		ret = -EFAULT;

	kvfree(datbuf);
	kvfree(oobbuf);

	return ret;
}
