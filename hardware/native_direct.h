/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Lab-only streaming-buffer comparison through the real ECC callbacks.
 * The module excludes UBI and holds both NAND locks. All commands are reads.
 */
static int sunxi_nfc_lab_direct_read(struct nand_chip *nand,
				     struct probe_request *r)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(nand->controller);
	void *private_buf = nfc->mdma_buf;
	u32 before[4], corrected = mtd->ecc_stats.corrected;
	u32 failed = mtd->ecc_stats.failed;
	u8 *allocation, *snapshot, *buf;
	dma_addr_t watch_dma;
	bool stable = true;
	unsigned int i;
	int ret;

	if (!nfc->caps->has_mdma_desc || !nfc->use_mdma || !nfc->reset ||
	    !private_buf || nfc->recovery_failed || mtd->writesize != 2048 ||
	    mtd->oobsize != 128 || nand->ecc.size != 1024 ||
	    nand->ecc.strength != 32 || r->page < 2688 || r->page >= 2752 ||
	    r->column || r->length != 2048 || r->seed > 3 || r->flags ||
	    r->chunk > 10000)
		return -EINVAL;
	allocation = kmalloc(4096, GFP_KERNEL);
	snapshot = kmalloc(4096, GFP_KERNEL);
	if (!allocation || !snapshot) {
		ret = -ENOMEM;
		goto free;
	}
	memset(allocation, 0xa5, 4096);
	buf = allocation + 128;
	memset(nand->oob_poi, 0xa5, 128);
	memcpy(before, sunxi_nfc_lab_counts, sizeof(before));
	memset(sunxi_nfc_lab_timeout_regs, 0, sizeof(sunxi_nfc_lab_timeout_regs));
	if (r->seed >= 2)
		/* Force the existing map_sg/unmap_sg path, without a data copy. */
		nfc->mdma_buf = NULL;
	sunxi_nfc_lab_timeout_delay = r->chunk;
	sunxi_nfc_lab_timeout_armed = r->seed == 3;
	if (!r->seed)
		ret = sunxi_nfc_hw_ecc_read_page(nand, buf, 1, r->page);
	else
		ret = sunxi_nfc_hw_ecc_read_page_dma(nand, buf, 1, r->page);
	if (sunxi_nfc_lab_timeout_armed)
		ret = -ENODATA;
	sunxi_nfc_lab_timeout_armed = false;
	nfc->mdma_buf = private_buf;
	r->regs[15] = 0xd200 | r->seed;
	if (nfc->recovery_failed) {
		/* The streaming SG may still belong to DMA: do not touch or free it.
		 * The probe pins itself and refuses all further hardware access.
		 */
		r->regs[15] |= BIT(31);
		return ret < 0 ? ret : -EIO;
	}
	r->regs[0] = mtd->ecc_stats.corrected - corrected;
	r->regs[1] = mtd->ecc_stats.failed - failed;
	memcpy(&r->regs[2], sunxi_nfc_lab_timeout_regs,
	       sizeof(sunxi_nfc_lab_timeout_regs));
	r->regs[5] = r->seed < 2 ||
		le32_to_cpu(nfc->mdma_desc->buf) == virt_to_phys(buf);
	for (i = 0; i < ARRAY_SIZE(before); i++)
		r->regs[10 + i] = sunxi_nfc_lab_counts[i] - before[i];
	r->regs[14] = !memchr_inv(allocation, 0xa5, 128) &&
		!memchr_inv(allocation + 2176, 0xa5, 4096 - 2176);
	memcpy(r->data, buf, 2048);
	memcpy(r->data + 2048, nand->oob_poi, 128);
	memcpy(snapshot, allocation, 4096);
	watch_dma = dma_map_single(nfc->dev, allocation, 4096, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(nfc->dev, watch_dma)) {
		ret = -ENOMEM;
		goto free;
	}
	if (watch_dma != virt_to_phys(allocation)) {
		ret = -EOPNOTSUPP;
		goto unmap;
	}
	for (i = 0; i < 4; i++) {
		if (i == 1)
			usleep_range(1000, 2000);
		else if (i == 2)
			msleep(20);
		else if (i == 3)
			msleep(80);
		dma_sync_single_for_cpu(nfc->dev, watch_dma, 4096, DMA_BIDIRECTIONAL);
		r->regs[6 + i] = !memcmp(allocation, snapshot, 4096);
		stable &= r->regs[6 + i];
		dma_sync_single_for_device(nfc->dev, watch_dma, 4096, DMA_BIDIRECTIONAL);
	}
	if (!stable || !r->regs[14]) {
		/* Preserve evidence and all possibly affected allocations. */
		nfc->recovery_failed = true;
		nfc->dma_unsafe = true;
		r->regs[15] |= BIT(31);
		return -EUCLEAN;
	}
unmap:
	dma_unmap_single(nfc->dev, watch_dma, 4096, DMA_BIDIRECTIONAL);
free:
	kfree(snapshot);
	kfree(allocation);
	return ret;
}
