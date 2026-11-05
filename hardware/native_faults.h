/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Lab-only callbacks. The module validates RAM-only/no UBI and block 7. */
static int sunxi_nfc_lab_request_fault(struct nand_chip *nand,
				       struct probe_request *r)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(nand->controller);
	u32 before[4], confirms = sunxi_nfc_lab_confirms;
	u32 failed = mtd->ecc_stats.failed;
	u8 *allocation, *buf;
	bool write = r->op == PROBE_ABORT_NATIVE_WRITE;
	unsigned int i;
	int ret;

	if (!nfc->caps->has_mdma_desc || !nfc->use_mdma || !nfc->reset ||
	    nfc->recovery_failed || mtd->writesize != 2048 ||
	    mtd->oobsize != 128 || nand->ecc.size != 1024 ||
	    nand->ecc.strength != 32 || r->page < 448 || r->page >= 512 ||
	    r->length != 2048 || r->chunk > 10000 ||
	    (write && (r->column || r->seed || r->flags)) ||
	    (!write && (r->column < 448 || r->column >= 512 ||
			r->column == r->page || r->seed < 1 || r->seed > 2 || r->flags > 3)))
		return -EINVAL;
	allocation = kmalloc(4096, GFP_KERNEL);
	if (!allocation)
		return -ENOMEM;
	memset(allocation, 0xa5, 4096);
	buf = allocation + 128;
	memcpy(before, sunxi_nfc_lab_counts, sizeof(before));
	r->regs[15] = write ? 0xd300 : 0xd400 | r->seed;
	if (write) {
		memcpy(buf, r->data, 2048);
		memcpy(nand->oob_poi, r->data + 2048, 128);
		nand->pagecache.page = -1;
		sunxi_nfc_lab_timeout_delay = r->chunk;
		sunxi_nfc_lab_timeout_armed = true;
		ret = sunxi_nfc_hw_ecc_write_page_dma(nand, buf, 1, r->page);
		if (sunxi_nfc_lab_timeout_armed)
			ret = -ENODATA;
		sunxi_nfc_lab_timeout_armed = false;
		if (nfc->recovery_failed)
			goto retain;
		memcpy(r->regs, sunxi_nfc_lab_timeout_regs,
		       sizeof(sunxi_nfc_lab_timeout_regs));
		r->regs[3] = sunxi_nfc_lab_confirms - confirms;
		r->regs[4] = le32_to_cpu(nfc->mdma_desc->buf) == virt_to_phys(buf);
		r->regs[5] = readl(nfc->regs + NFC_REG_H6_MDMA_STA);
		r->regs[6] = readl(nfc->regs + 0x214);
		/* Retain the actual source allocation for observation. Stable RAM
		 * cannot detect device READS, so this is not a DMA-retirement proof.
		 */
		msleep(100);
		r->regs[7] = readl(nfc->regs + NFC_REG_H6_MDMA_STA);
		r->regs[8] = readl(nfc->regs + 0x214);
		r->regs[9] = !memcmp(buf, r->data, 2048);
	} else {
		memset(nand->oob_poi, 0xa5, 128);
		memset(sunxi_nfc_lab_pio_regs, 0, sizeof(sunxi_nfc_lab_pio_regs));
		sunxi_nfc_lab_pio_delay = r->chunk;
		sunxi_nfc_lab_pio_stage = r->seed;
		ret = sunxi_nfc_hw_ecc_read_page(nand, buf, 1, r->page);
		r->regs[9] = !sunxi_nfc_lab_pio_stage;
		r->regs[9] |= !memcmp(buf, r->data, 2048) << 1;
		r->regs[9] |= !memcmp(nand->oob_poi, r->data + 2048, 128) << 2;
		sunxi_nfc_lab_pio_stage = 0;
		r->regs[0] = ret;
		memcpy(&r->regs[1], sunxi_nfc_lab_pio_regs,
		       sizeof(sunxi_nfc_lab_pio_regs));
		r->regs[7] = mtd->ecc_stats.failed - failed;
		/* No module-level idle/reset/context restoration between requests.
		 * Record the first follow-up read; never discard a recovery read.
		 */
		if (r->flags & BIT(1))
			udelay(1000);
		r->regs[5] = readl(nfc->regs + NFC_REG_ST);
		memset(buf, 0x69, 2048);
		memset(nand->oob_poi, 0x69, 128);
		failed = mtd->ecc_stats.failed;
		if (r->flags & BIT(0))
			ret = sunxi_nfc_hw_ecc_read_page_dma(nand, buf, 1, r->column);
		else
			ret = sunxi_nfc_hw_ecc_read_page(nand, buf, 1, r->column);
		if (nfc->recovery_failed)
			goto retain;
		r->regs[6] = !!(r->flags & BIT(0));
		r->regs[8] = mtd->ecc_stats.failed - failed;
		memcpy(r->data, buf, 2048);
		memcpy(r->data + 2048, nand->oob_poi, 128);
	}
	for (i = 0; i < ARRAY_SIZE(before); i++)
		r->regs[10 + i] = sunxi_nfc_lab_counts[i] - before[i];
	r->regs[14] = !memchr_inv(allocation, 0xa5, 128) &&
		!memchr_inv(allocation + 2176, 0xa5, 4096 - 2176);
	if (!r->regs[14]) {
		nfc->recovery_failed = true;
		ret = -EOVERFLOW;
		goto retain;
	}
	kfree(allocation);
	return ret;
retain:
	/* Do not release possibly device-owned RAM; the probe pins itself. */
	r->regs[15] |= BIT(31);
	return ret < 0 ? ret : -EIO;
}
