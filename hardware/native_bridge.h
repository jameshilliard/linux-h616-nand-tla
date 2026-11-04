/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only appendix to sunxi_nand.c. Never install in a production kernel. */
#include "sunxi_nand_lab_api.h"

int sunxi_nfc_lab_native(struct nand_chip *nand, struct probe_request *r);
int sunxi_nfc_lab_native(struct nand_chip *nand, struct probe_request *r)
{
	struct mtd_info *mtd = nand_to_mtd(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(nand->controller);
	u32 before[4];
	u8 *allocation, *buf;
	bool write = r->op == PROBE_NATIVE_WRITE ||
		r->op == PROBE_NATIVE_SUBWRITE || r->op == PROBE_NATIVE_OOB_WRITE ||
		r->op == PROBE_NATIVE_RAW_WRITE;
	unsigned int i;
	int ret;

	lockdep_assert_held(&nand->lock);
	lockdep_assert_held(&nand->controller->lock);
	if (!nfc->caps->has_mdma_desc || !nfc->use_mdma ||
	    mtd->writesize != 2048 || mtd->oobsize != 128 ||
	    nand->ecc.size != 1024 || nand->ecc.strength != 32 ||
	    r->page >= 512 || (write && r->page < 448) || r->seed > 2 ||
	    r->column >= 2048 || !r->length || r->length > 2048 - r->column)
		return -EINVAL;
	/* Cacheline-separated guards, not adjacent bytes in a DMA-owned line. */
	allocation = kmalloc(4096, GFP_KERNEL);
	if (!allocation)
		return -ENOMEM;
	memset(allocation, 0xa5, 4096);
	buf = allocation + 128;
	memcpy(buf, r->data, 2048);
	memcpy(nand->oob_poi, r->data + 2048, 128);
	memcpy(before, sunxi_nfc_lab_counts, sizeof(before));
	if (write)
		nand->pagecache.page = -1;
	switch (r->op) {
	case PROBE_NATIVE_READ:
		if (r->seed == 0)
			ret = sunxi_nfc_hw_ecc_read_page(nand, buf, 1, r->page);
		else if (r->seed == 1)
			ret = sunxi_nfc_hw_ecc_read_page_dma(nand, buf, 1, r->page);
		else
			ret = nand->ecc.read_page(nand, buf, 1, r->page);
		break;
	case PROBE_NATIVE_WRITE:
		if (r->seed == 0)
			ret = sunxi_nfc_hw_ecc_write_page(nand, buf, 1, r->page);
		else if (r->seed == 1)
			ret = sunxi_nfc_hw_ecc_write_page_dma(nand, buf, 1, r->page);
		else
			ret = nand->ecc.write_page(nand, buf, 1, r->page);
		break;
	case PROBE_NATIVE_SUBREAD:
		if (r->seed == 0)
			ret = sunxi_nfc_hw_ecc_read_subpage(nand, r->column, r->length, buf, r->page);
		else
			ret = sunxi_nfc_hw_ecc_read_subpage_dma(nand, r->column, r->length, buf, r->page);
		break;
	case PROBE_NATIVE_SUBWRITE:
		ret = sunxi_nfc_hw_ecc_write_subpage(nand, r->column, r->length, buf, 1, r->page);
		break;
	case PROBE_NATIVE_OOB_READ:
		ret = nand->ecc.read_oob(nand, r->page);
		break;
	case PROBE_NATIVE_OOB_WRITE:
		ret = nand->ecc.write_oob(nand, r->page);
		break;
	case PROBE_NATIVE_RAW_READ:
		ret = nand->ecc.read_page_raw(nand, buf, 1, r->page);
		break;
	case PROBE_NATIVE_RAW_WRITE:
		ret = nand->ecc.write_page_raw(nand, buf, 1, r->page);
		break;
	default:
		ret = -EINVAL;
	}
	memcpy(r->data, buf, 2048);
	memcpy(r->data + 2048, nand->oob_poi, 128);
	for (i = 0; i < ARRAY_SIZE(before); i++)
		r->regs[10 + i] = sunxi_nfc_lab_counts[i] - before[i];
	r->regs[14] = 1;
	for (i = 0; i < 128; i++)
		if (allocation[i] != 0xa5 || allocation[2176 + i] != 0xa5)
			r->regs[14] = 0;
	if (!r->regs[14])
		ret = -EOVERFLOW;
	r->regs[15] = 0x10;
	kfree(allocation);
	return ret;
}
EXPORT_SYMBOL_GPL(sunxi_nfc_lab_native);
