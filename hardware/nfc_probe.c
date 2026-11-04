// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Diagnostic only, not a replacement NAND driver. Build against the exact
 * running kernel. All accesses share the NAND core's chip/controller locks.
 * Writes are deliberately limited to block 7 of a verified 2K+128 H616 NAND.
 * Never load on a machine where this backup SPL block is not expendable.
 */
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mtd/rawnand.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "probe.h"

#define CTL 0x00
#define ST 0x04
#define INT_EN 0x08
#define CNT 0x20
#define CMD 0x24
#define ECC_CTL 0x34
#define ECC_ST 0x38
#define PAT_FOUND 0x3c
#define EFR 0x40
#define ERR_CNT 0x50
#define USER_LEN 0x70
#define SPARE 0x114
#define PAT_ID 0x118
#define RAM0 0x400
#define RAM1 0x800
#define DONE BIT(1)
#define BUSY (BIT(3) | BIT(4))
#define READ_DATA (BIT(21) | BIT(26))
#define FIRST_PAGE 448
#define END_PAGE 512

static bool allow_write;
module_param(allow_write, bool, 0400);
MODULE_PARM_DESC(allow_write, "Permit block 7 erase/program AFTER raw backup verification");
static struct mtd_info *partition, *master;
static struct nand_chip *chip;
static struct platform_device *pdev;
static void __iomem *regs;
static int irq;

static int idle(void)
{
	u32 value;
	return readl_poll_timeout(regs + ST, value, !(value & BUSY), 1, 100000);
}

static int transfer(u32 count, u32 command)
{
	u32 value;
	int ret = idle();
	if (ret)
		return ret;
	writel(DONE, regs + ST);
	writel(count, regs + CNT);
	writel(command, regs + CMD);
	ret = readl_poll_timeout(regs + ST, value, value & DONE, 1, 100000);
	if (!ret)
		ret = idle();
	return ret;
}

static u32 random_config(const struct probe_request *r)
{
	return (r->flags & PROBE_RANDOM) ?
		(r->seed << 16) | BIT(5) |
		((r->flags & PROBE_PIPELINE) ? BIT(3) : 0) |
		((r->flags & PROBE_PAGE_RANDOM) ? BIT(7) : 0) : 0;
}

static int stream(struct probe_request *r, bool write)
{
	u32 offset, n, config = random_config(r);
	int ret;

	if (write)
		ret = nand_prog_page_begin_op(chip, r->page, r->column, NULL, 0);
	else
		ret = nand_read_page_op(chip, r->page, r->column, NULL, 0);
	if (ret)
		return ret;
	writel(readl(regs + CTL) & ~BIT(14), regs + CTL);
	writel(config, regs + ECC_CTL);
	for (offset = 0; offset < r->length; offset += n) {
		n = min(r->chunk, r->length - offset);
		if (r->flags & PROBE_RESEED) {
			writel(0, regs + ECC_CTL);
			writel(config, regs + ECC_CTL);
		}
		if (write)
			memcpy_toio(regs + RAM0, r->data + offset, n);
		ret = transfer(n, READ_DATA | (write ? BIT(20) : 0));
		if (ret)
			return ret;
		if (!write) {
			if (r->flags & PROBE_WORD_READ) {
				unsigned int j, k;
				for (j = 0; j < n; j += 4) {
					u32 word = readl(regs + RAM0 + j);
					for (k = 0; k < min(4U, n - j); k++)
						r->data[offset + j + k] = word >> (k * 8);
				}
			} else {
				memcpy_fromio(r->data + offset, regs + RAM0, n);
			}
		}
	}
	r->regs[0] = readl(regs + CNT);
	r->regs[1] = readl(regs + ST);
	r->regs[2] = readl(regs + ECC_CTL);
	writel(0, regs + ECC_CTL);
	writel(DONE, regs + ST);
	return write ? nand_prog_page_end_op(chip) : 0;
}

static int count_test(struct probe_request *r)
{
	int ret = nand_read_page_op(chip, r->page, 0, NULL, 0);
	if (ret)
		return ret;
	writel(readl(regs + CTL) & ~BIT(14), regs + CTL);
	writel(0, regs + ECC_CTL);
	memset_io(regs + RAM0, 0xa5, 1024);
	ret = transfer(r->chunk, READ_DATA);
	r->regs[0] = readl(regs + CNT);
	r->regs[1] = readl(regs + ST);
	memcpy_fromio(r->data, regs + RAM0, 1024);
	writel(DONE, regs + ST);
	return ret;
}

static int banks_test(struct probe_request *r)
{
	u8 *saved = kmalloc(2048, GFP_KERNEL);
	unsigned int i;
	if (!saved)
		return -ENOMEM;
	writel(readl(regs + CTL) & ~BIT(14), regs + CTL);
	memcpy_fromio(saved, regs + RAM0, 1024);
	memcpy_fromio(saved + 1024, regs + RAM1, 1024);
	for (i = 0; i < 256; i++)
		writel(0x13579bdf ^ i, regs + RAM0 + i * 4);
	for (i = 0; i < 256; i++)
		writel(0xeca86420 ^ i, regs + RAM1 + i * 4);
	memcpy_fromio(r->data, regs + RAM0, 1024);
	memcpy_fromio(r->data + 1024, regs + RAM1, 1024);
	memcpy_toio(regs + RAM0, saved, 1024);
	memcpy_toio(regs + RAM1, saved + 1024, 1024);
	kfree(saved);
	return 0;
}

static int validate(const struct probe_request *r)
{
	if (r->op > PROBE_EVENTS || r->flags & ~31U || r->seed > 0x7fff)
		return -EINVAL;
	if (r->op == PROBE_INFO || r->op == PROBE_BANKS)
		return 0;
	if (r->page >= END_PAGE || r->column > PROBE_RAW_SIZE ||
	    r->length > PROBE_RAW_SIZE - r->column)
		return -EINVAL;
	if (r->op == PROBE_COUNT)
		return r->chunk <= 2049 ? 0 : -EINVAL;
	if (r->op == PROBE_READ || r->op == PROBE_PROGRAM)
		if (!r->length || !r->chunk || r->chunk > 1024)
			return -EINVAL;
	if (r->op == PROBE_ERASE || r->op == PROBE_PROGRAM || r->op == PROBE_ECC_WRITE)
		if (!allow_write || r->page < FIRST_PAGE)
			return -EROFS;
	if (r->op == PROBE_ERASE && r->page != FIRST_PAGE)
		return -EINVAL;
	if (r->op == PROBE_PIO_ECC_READ && r->column != 0 && r->column != 1024)
		return -EINVAL;
	return 0;
}

static int pio_ecc_read(struct probe_request *r)
{
	u32 config = (r->seed << 16) | (3 << 8) | BIT(5) | BIT(4) | BIT(3) | BIT(0);
	u32 user = r->column ? 0 : 8;
	u32 spare_column = r->column ? 2112 : 2048;
	unsigned int i;
	int ret = nand_read_page_op(chip, r->page, r->column, NULL, 0);
	if (ret)
		return ret;
	writel(readl(regs + CTL) & ~BIT(14), regs + CTL);
	for (i = 0; i < 4; i++)
		writel(0, regs + USER_LEN + i * 4);
	writel(config, regs + ECC_CTL);
	ret = transfer(1024, READ_DATA);
	if (ret)
		return ret;
	writel(config & ~BIT(5), regs + ECC_CTL);
	ret = nand_change_read_column_op(chip, spare_column, NULL, 0, false);
	if (ret)
		return ret;
	writel(user / 4, regs + USER_LEN);
	writel(config, regs + ECC_CTL);
	ret = transfer(1024, READ_DATA | BIT(30));
	if (ret)
		return ret;
	r->regs[0] = readl(regs + ECC_ST);
	r->regs[1] = readl(regs + ERR_CNT);
	r->regs[2] = readl(regs + PAT_FOUND);
	r->regs[3] = readl(regs + PAT_ID);
	r->regs[4] = readl(regs + ECC_CTL);
	memcpy_fromio(r->data, regs + RAM0, 1024);
	for (i = 0; i < user; i += 4) {
		u32 word = readl(regs + 0x80 + i);
		memcpy(r->data + 2048 + i, &word, 4);
	}
	writel(DONE, regs + ST);
	return 0;
}

static long probe_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
	struct probe_request *r;
	u8 saved_oob[128];
	u32 ctl, ecc_ctl, int_en, efr, cnt, spare;
	u32 lengths[4];
	unsigned int corrected, failed;
	int ret;

	if (!capable(CAP_SYS_RAWIO) || command != PROBE_IOC)
		return -EPERM;
	r = memdup_user((void __user *)arg, sizeof(*r));
	if (IS_ERR(r))
		return PTR_ERR(r);
	ret = validate(r);
	if (ret)
		goto output;
	/* Same ordering as nand_get_device(); do not access suspended hardware. */
	mutex_lock(&chip->lock);
	if (chip->suspended) {
		ret = -EBUSY;
		goto unlock_chip;
	}
	mutex_lock(&chip->controller->lock);
	nand_select_target(chip, 0);
	ret = idle();
	if (ret)
		goto unlock;
	int_en = readl(regs + INT_EN);
	if (int_en) {
		ret = -EBUSY;
		goto unlock;
	}
	synchronize_irq(irq);
	ctl = readl(regs + CTL);
	ecc_ctl = readl(regs + ECC_CTL);
	efr = readl(regs + EFR);
	cnt = readl(regs + CNT);
	spare = readl(regs + SPARE);
	for (unsigned int i = 0; i < 4; i++)
		lengths[i] = readl(regs + USER_LEN + i * 4);
	memcpy(saved_oob, chip->oob_poi, sizeof(saved_oob));
	corrected = master->ecc_stats.corrected;
	failed = master->ecc_stats.failed;
	writel(0, regs + ECC_CTL);
	switch (r->op) {
	case PROBE_INFO:
		r->regs[0] = master->writesize;
		r->regs[1] = master->oobsize;
		r->regs[2] = master->erasesize;
		r->regs[3] = chip->ecc.size;
		r->regs[4] = chip->ecc.strength;
		r->regs[5] = chip->ecc.bytes;
		r->regs[6] = chip->ecc.steps;
		r->regs[7] = chip->options;
		r->regs[8] = ctl;
		r->regs[9] = ecc_ctl;
		r->regs[10] = readl(regs + USER_LEN);
		r->regs[11] = spare;
		r->regs[12] = efr;
		r->regs[13] = chip->buf_align;
		break;
	case PROBE_READ:
		ret = stream(r, false);
		break;
	case PROBE_COUNT:
		ret = count_test(r);
		break;
	case PROBE_BANKS:
		ret = banks_test(r);
		break;
	case PROBE_ERASE:
		chip->pagecache.page = -1;
		ret = nand_erase_op(chip, FIRST_PAGE / 64);
		break;
	case PROBE_PROGRAM:
		chip->pagecache.page = -1;
		ret = stream(r, true);
		break;
	case PROBE_ECC_READ:
		memset(chip->oob_poi, 0xff, 128);
		ret = chip->ecc.read_page(chip, r->data, true, r->page);
		memcpy(r->data + 2048, chip->oob_poi, 128);
		break;
	case PROBE_ECC_WRITE:
		chip->pagecache.page = -1;
		memcpy(chip->oob_poi, r->data + 2048, 128);
		ret = chip->ecc.write_page(chip, r->data, true, r->page);
		break;
	case PROBE_PIO_ECC_READ:
		ret = pio_ecc_read(r);
		break;
	case PROBE_EVENTS:
		ret = nand_read_page_op(chip, r->page, 0, NULL, 0);
		if (!ret)
			ret = transfer(4, READ_DATA);
		r->regs[0] = readl(regs + ST);
		writel(0, regs + ST);
		r->regs[1] = readl(regs + ST);
		writel(BIT(0), regs + ST);
		r->regs[2] = readl(regs + ST);
		writel(DONE, regs + ST);
		r->regs[3] = readl(regs + ST);
		break;
	}
	if (r->op == PROBE_ECC_READ || r->op == PROBE_ECC_WRITE) {
		r->regs[0] = master->ecc_stats.corrected - corrected;
		r->regs[1] = master->ecc_stats.failed - failed;
		r->regs[2] = readl(regs + ECC_ST);
		r->regs[3] = readl(regs + PAT_FOUND);
		r->regs[4] = readl(regs + PAT_ID);
		r->regs[5] = readl(regs + ERR_CNT);
		r->regs[6] = readl(regs + USER_LEN);
	}
	memcpy(chip->oob_poi, saved_oob, sizeof(saved_oob));
	/* Synthetic errors must not change production MTD health statistics. */
	master->ecc_stats.corrected = corrected;
	master->ecc_stats.failed = failed;
	writel(ecc_ctl, regs + ECC_CTL);
	writel(efr, regs + EFR);
	writel(cnt, regs + CNT);
	writel(spare, regs + SPARE);
	for (unsigned int i = 0; i < 4; i++)
		writel(lengths[i], regs + USER_LEN + i * 4);
	writel(ctl, regs + CTL);
	writel(int_en, regs + INT_EN);
unlock:
	nand_deselect_target(chip);
	mutex_unlock(&chip->controller->lock);
unlock_chip:
	mutex_unlock(&chip->lock);
output:
	r->result = ret;
	ret = copy_to_user((void __user *)arg, r, sizeof(*r)) ? -EFAULT : 0;
	kfree(r);
	return ret;
}

static const struct file_operations probe_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = probe_ioctl,
};
static struct miscdevice probe_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "nfc_probe",
	.fops = &probe_ops,
	.mode = 0600,
};

static int __init probe_init(void)
{
	struct device_node *np;
	struct resource resource;
	int ret;

	partition = get_mtd_device_nm("spl-3");
	if (IS_ERR(partition))
		return PTR_ERR(partition);
	master = mtd_get_master(partition);
	ret = -ENODEV;
	if (mtd_get_master_ofs(partition, 0) != 0xc0000 ||
	    partition->size != 0x40000 || master->writesize != 2048 ||
	    master->oobsize != 128 || master->erasesize != 0x20000 ||
	    master->size != 0x10000000 || master->type != MTD_NANDFLASH)
		goto put_mtd;
	chip = mtd_to_nand(master);
	if (chip->ecc.size != 1024 || chip->ecc.steps != 2 ||
	    chip->ecc.strength != 32 || chip->ecc.bytes != 56)
		goto put_mtd;
	np = of_find_compatible_node(NULL, NULL, "allwinner,sun50i-h616-nand-controller");
	if (!np)
		goto put_mtd;
	pdev = of_find_device_by_node(np);
	ret = of_address_to_resource(np, 0, &resource);
	of_node_put(np);
	if (!pdev || ret || resource.start != 0x04011000 || resource_size(&resource) < 0xc00) {
		ret = -ENODEV;
		goto put_platform;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto put_platform;
	}
	regs = ioremap(resource.start, resource_size(&resource));
	if (!regs) {
		ret = -ENOMEM;
		goto put_platform;
	}
	ret = misc_register(&probe_device);
	if (!ret) {
		pr_info("nfc_probe: block 7 diagnostic, writes %s\n", allow_write ? "armed" : "disabled");
		return 0;
	}
	iounmap(regs);
put_platform:
	if (pdev)
		put_device(&pdev->dev);
put_mtd:
	put_mtd_device(partition);
	return ret;
}

static void __exit probe_exit(void)
{
	misc_deregister(&probe_device);
	iounmap(regs);
	put_device(&pdev->dev);
	put_mtd_device(partition);
}
module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Hilliard");
MODULE_DESCRIPTION("Restricted H616 NFC hardware-contract diagnostic");
