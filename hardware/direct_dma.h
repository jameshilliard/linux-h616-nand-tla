/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Restricted lab test, included by nfc_probe.c. NAND traffic is READ ONLY.
 * The buffers remain allocated for the complete observation window. Reuse is
 * logical reuse in test-owned storage, never memory handed to another owner.
 */
static const u16 direct_offsets[] = {
	CTL, 0x0c, 0x10, 0x14, 0x18, 0x1c, CNT, 0x28, 0x2c,
	ECC_CTL, EFR, 0x70, 0x74, 0x78, 0x7c, SPARE,
};

static void direct_restore(const u32 *saved)
{
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(direct_offsets); i++)
		writel(saved[i], regs + direct_offsets[i]);
	writel(saved[0], regs + CTL);
	writel(0, regs + INT_EN);
	writel(BIT(0) | DONE | BIT(2), regs + ST);
}

static int direct_launch(__le32 *desc, dma_addr_t desc_dma,
			 dma_addr_t data_dma, unsigned int page)
{
	int ret = nand_read_page_op(chip, page, 0, NULL, 0);

	if (ret)
		return ret;
	memset(desc, 0, 64);
	desc[0] = cpu_to_le32(BIT(3) | BIT(2));
	desc[1] = cpu_to_le32(2048);
	desc[2] = cpu_to_le32(data_dma);
	desc[3] = cpu_to_le32(desc_dma);
	writel(0, regs + INT_EN);
	writel(BIT(0) | DONE | BIT(2), regs + ST);
	writel(1, regs + 0x204);
	writel((readl(regs + CTL) | BIT(14)) & ~BIT(15), regs + CTL);
	writel(3, regs + 0x1c);
	writel(1024, regs + CNT);
	writel(2048, regs + SPARE);
	/* Raw main-area read: no ECC correction or randomization. */
	writel(0, regs + ECC_CTL);
	writel(0, regs + USER_LEN);
	writel(0, regs + USER_LEN + 4);
	writel(0, regs + USER_LEN + 8);
	writel(0, regs + USER_LEN + 12);
	writel((NAND_CMD_RNDOUTSTART << 16) | (NAND_CMD_RNDOUT << 8) |
	       NAND_CMD_READSTART, regs + 0x28);
	dma_wmb();
	writel(desc_dma, regs + 0x200);
	writel(BIT(31) | READ_DATA, regs + CMD);
	return 0;
}

static int direct_complete(void)
{
	u32 value;

	return readl_poll_timeout(regs + ST, value,
		(value & (DONE | BIT(2))) == (DONE | BIT(2)) &&
		!(value & BUSY), 1, 100000);
}

static bool direct_stable(void *buf, dma_addr_t dma)
{
	bool stable;

	/* Invalidate the CPU view on non-coherent ARM64. A mere dma_rmb()
	 * would not expose an unexpected write hidden by cached poison bytes.
	 */
	dma_sync_single_for_cpu(&pdev->dev, dma, 4096, DMA_BIDIRECTIONAL);
	stable = !memchr_inv(buf, 0x69, 4096);
	dma_sync_single_for_device(&pdev->dev, dma, 4096, DMA_BIDIRECTIONAL);
	return stable;
}

static int direct_retire_test(struct probe_request *r)
{
	int (*bus_reset)(struct nand_chip *);
	u32 saved[ARRAY_SIZE(direct_offsets)], value;
	u8 *first, *next;
	__le32 *desc;
	dma_addr_t first_dma, next_dma, desc_dma, watch_dma = 0;
	unsigned int i;
	int ret, reset_ret;
	bool first_mapped = false, next_mapped = false, watching = false;

	bus_reset = __symbol_get("sunxi_nfc_lab_bus_reset");
	if (!bus_reset)
		return -EOPNOTSUPP;
	first = kmalloc(4096, GFP_KERNEL);
	next = kmalloc(4096, GFP_KERNEL);
	desc = dma_alloc_coherent(&pdev->dev, 64, &desc_dma, GFP_KERNEL);
	if (!first || !next || !desc) {
		ret = -ENOMEM;
		goto free;
	}
	memset(first, 0xa5, 4096);
	memset(next, 0xa5, 4096);
	first_dma = dma_map_single(&pdev->dev, first, 4096, DMA_FROM_DEVICE);
	if (dma_mapping_error(&pdev->dev, first_dma)) {
		ret = -ENOMEM;
		goto free;
	}
	first_mapped = true;
	next_dma = dma_map_single(&pdev->dev, next, 4096, DMA_FROM_DEVICE);
	if (dma_mapping_error(&pdev->dev, next_dma)) {
		ret = -ENOMEM;
		goto unmap;
	}
	next_mapped = true;
	/* Require direct physical mappings, not an IOMMU or SWIOTLB substitute.
	 * Otherwise the watcher might monitor different memory from the engine.
	 */
	if (upper_32_bits(first_dma) || upper_32_bits(next_dma) ||
	    upper_32_bits(desc_dma) || first_dma != virt_to_phys(first) ||
	    next_dma != virt_to_phys(next)) {
		ret = -EOPNOTSUPP;
		goto unmap;
	}
	r->regs[12] = 1;
	for (i = 0; i < ARRAY_SIZE(direct_offsets); i++)
		saved[i] = readl(regs + direct_offsets[i]);
	disable_irq(irq);
	ret = direct_launch(desc, desc_dma, first_dma + 128, r->page);
	if (ret)
		goto recover;
	if (r->flags == 0 || r->flags == 3)
		ret = direct_complete();
	else if (r->chunk)
		udelay(r->chunk);
	r->regs[0] = readl(regs + ST);
	r->regs[1] = readl(regs + 0x204);
	r->regs[2] = readl(regs + 0x214);
	if (r->flags != 2) {
		writel(0, regs + ECC_CTL);
		writel(BIT(1), regs + CTL);
		reset_ret = readl_poll_timeout(regs + CTL, value,
					      !(value & BIT(1)), 1, 100000);
		if (reset_ret)
			ret = reset_ret;
	}
	reset_ret = bus_reset(chip);
	if (reset_ret)
		goto poison;
	r->regs[3] = readl(regs + CTL);
	r->regs[4] = readl(regs + ST);
	r->regs[5] = readl(regs + 0x214);
	/* This is the candidate ownership boundary, without a settling sleep. */
	dma_unmap_single(&pdev->dev, first_dma, 4096, DMA_FROM_DEVICE);
	first_mapped = false;
	r->regs[6] = !memchr_inv(first, 0xa5, 128) &&
		!memchr_inv(first + 2176, 0xa5, 4096 - 2176);
	memset(first, 0x69, 4096);
	watch_dma = dma_map_single(&pdev->dev, first, 4096, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&pdev->dev, watch_dma)) {
		ret = -ENOMEM;
		goto recover;
	}
	watching = true;
	if (watch_dma != first_dma) {
		ret = -EOPNOTSUPP;
		goto recover;
	}
	r->regs[7] = direct_stable(first, watch_dma);
	direct_restore(saved);
	if (!ret)
		/* Mode 3 deliberately writes the poison buffer: detector control. */
		ret = direct_launch(desc, desc_dma,
			(r->flags == 3 ? watch_dma : next_dma) + 128, r->seed);
	if (!ret)
		ret = direct_complete();
	if (ret)
		goto recover;
	r->regs[13] = readl(regs + ST);
	r->regs[14] = readl(regs + 0x214);
	dma_unmap_single(&pdev->dev, next_dma, 4096, DMA_FROM_DEVICE);
	next_mapped = false;
	memcpy(r->data, next + 128, 2048);
	r->regs[6] &= !memchr_inv(next, 0xa5, 128) &&
		!memchr_inv(next + 2176, 0xa5, 4096 - 2176);
	r->regs[8] = direct_stable(first, watch_dma);
	usleep_range(1000, 2000);
	r->regs[9] = direct_stable(first, watch_dma);
	msleep(20);
	r->regs[10] = direct_stable(first, watch_dma);
	msleep(80);
	r->regs[11] = direct_stable(first, watch_dma);
	r->regs[15] = 0xd100 | r->flags;
	if (!r->regs[6] || !r->regs[7] ||
	    r->regs[8] != (r->flags != 3) ||
	    r->regs[9] != (r->flags != 3) ||
	    r->regs[10] != (r->flags != 3) ||
	    r->regs[11] != (r->flags != 3)) {
		reset_ret = -EUCLEAN;
		goto poison;
	}
recover:
	/* Even failed setup/completion retains allocations until bus reset. */
	reset_ret = bus_reset(chip);
	if (reset_ret)
		goto poison;
	direct_restore(saved);
	enable_irq(irq);
unmap:
	if (watching)
		dma_unmap_single(&pdev->dev, watch_dma, 4096, DMA_BIDIRECTIONAL);
	if (next_mapped)
		dma_unmap_single(&pdev->dev, next_dma, 4096, DMA_FROM_DEVICE);
	if (first_mapped)
		dma_unmap_single(&pdev->dev, first_dma, 4096, DMA_FROM_DEVICE);
free:
	if (desc)
		dma_free_coherent(&pdev->dev, 64, desc, desc_dma);
	kfree(next);
	kfree(first);
	__symbol_put("sunxi_nfc_lab_bus_reset");
	return ret;
poison:
	/* No unmap/free or further register access on an unproven stop. */
	poisoned = true;
	__module_get(THIS_MODULE);
	pr_err("nfc_probe: direct DMA stop failed; allocations pinned, power cycle required\n");
	__symbol_put("sunxi_nfc_lab_bus_reset");
	return reset_ret;
}
