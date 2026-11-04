// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Diagnostic only, not a replacement NAND driver. Build against the exact
 * running kernel. All accesses share the NAND core's chip/controller locks.
 * Writes are deliberately limited to block 7 of a verified 2K+128 H616 NAND.
 * Never load on a machine where this backup SPL block is not expendable.
 */
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/ubi.h>
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
static bool allow_recovery;
module_param(allow_recovery, bool, 0400);
MODULE_PARM_DESC(allow_recovery, "RAM-only/no UBI: permit read-side MDMA reset observations");
static struct mtd_info *partition, *master;
static struct nand_chip *chip;
static struct platform_device *pdev;
static void __iomem *regs;
static int irq;
static void *recovery_mem, *recovery_desc;
static dma_addr_t recovery_dma, recovery_desc_dma;
static bool poisoned;

static int no_attached_ubi(void)
{
	struct ubi_device_info info;
	int ret, number;

	for (number = 0; number < 256; number++) {
		ret = ubi_get_device_info(number, &info);
		if (ret == -EINVAL)
			return 0; /* End of the UBI device-number range. */
		if (ret != -ENODEV)
			return ret ? ret : -EBUSY;
	}
	return -E2BIG;
}

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
		((r->flags & PROBE_MSB) ? BIT(6) : 0) |
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
		if (r->flags & PROBE_TOGGLE) {
			if (offset == r->chunk)
				writel(config & ~BIT(5), regs + ECC_CTL);
			if (offset == 2 * r->chunk)
				writel(config, regs + ECC_CTL);
		}
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
	if (poisoned)
		return -EIO;
	if (r->op > PROBE_MASKED_NATIVE_READ || r->op == 22 || r->op == 23 ||
	    r->flags & ~127U || r->seed > 0x7fff)
		return -EINVAL;
	if (r->op == PROBE_INFO || r->op == PROBE_BANKS)
		return 0;
	if (r->page >= END_PAGE || r->column > PROBE_RAW_SIZE ||
	    r->length > PROBE_RAW_SIZE - r->column)
		return -EINVAL;
	if (r->op == PROBE_COUNT)
		return r->chunk <= 2049 ? 0 : -EINVAL;
	if (r->op == PROBE_READ || r->op == PROBE_PROGRAM || r->op == PROBE_TRANSITIONS)
		if (!r->length || !r->chunk || r->chunk > 1024)
			return -EINVAL;
	if (r->op == PROBE_ERASE || r->op == PROBE_PROGRAM || r->op == PROBE_ECC_WRITE ||
	    r->op == PROBE_NATIVE_WRITE || r->op == PROBE_NATIVE_SUBWRITE ||
	    r->op == PROBE_NATIVE_OOB_WRITE || r->op == PROBE_NATIVE_RAW_WRITE)
		if (!allow_write || r->page < FIRST_PAGE)
			return -EROFS;
	if (r->op == PROBE_ERASE && r->page != FIRST_PAGE)
		return -EINVAL;
	if (r->op == PROBE_PIO_ECC_READ && r->column != 0 && r->column != 1024)
		return -EINVAL;
	if (r->op == PROBE_CODEWORD_READ) {
		static const u16 parity[] = { 28, 42, 50, 56, 70, 78, 84, 92,
			98, 106, 112, 120, 126, 134, 140 };
		u32 mode = r->chunk & 255, user = (r->chunk >> 8) & 255;
		u32 spare = r->chunk >> 16;

		if (mode >= ARRAY_SIZE(parity) || user > 32 || user % 4 ||
		    r->length != 1024 || r->column + 1024 > PROBE_RAW_SIZE ||
		    spare + user + parity[mode] > PROBE_RAW_SIZE)
			return -EINVAL;
	}
	if (r->op == PROBE_RECOVERY &&
	    (!allow_recovery || r->page < FIRST_PAGE || r->chunk > 10000 || r->flags > 5))
		return -EPERM;
	if (r->op == PROBE_RECOVERY)
		return no_attached_ubi();
	if (r->op == PROBE_MASKED_NATIVE_READ) {
		if (!allow_recovery || r->page < FIRST_PAGE)
			return -EPERM;
		return no_attached_ubi();
	}
	if (r->op == PROBE_ACTUAL_SUBREAD &&
	    (r->column >= 2048 || !r->length || r->length > 2048 - r->column))
		return -EINVAL;
	return 0;
}

static int pio_ecc_read(struct probe_request *r)
{
	u32 config = (r->seed << 16) | (3 << 8) | BIT(5) | BIT(4) | BIT(3) | BIT(0);
	u32 user = r->column ? 0 : 8;
	u32 spare_column = r->column ? 2112 : 2048;
	unsigned int i;
	int ret;

	if (r->op == PROBE_CODEWORD_READ) {
		user = (r->chunk >> 8) & 255;
		spare_column = r->chunk >> 16;
		config = (r->chunk & 255) << 8 | BIT(4) | BIT(0) | random_config(r);
	}
	ret = nand_read_page_op(chip, r->page, r->column, NULL, 0);

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

static int native_request(struct probe_request *r)
{
	int (*call)(struct nand_chip *, struct probe_request *);
	int ret;

	call = __symbol_get("sunxi_nfc_lab_native");
	if (!call)
		return -EOPNOTSUPP;
	if (r->flags & PROBE_TOGGLE) {
		/* Deliberately stale, valid two-slot layout; callbacks must replace it. */
		writel(0x22, regs + USER_LEN);
		if (r->flags & PROBE_MSB)
			for (unsigned int i = 1; i < 4; i++)
				writel(0x11, regs + USER_LEN + i * 4);
		for (unsigned int i = 0; i < 32; i++)
			writel(0xdecafbad, regs + 0x80 + i * 4);
	}
	ret = call(chip, r);
	__symbol_put("sunxi_nfc_lab_native");
	return ret;
}

static int actual_subread(struct probe_request *r)
{
	u8 *allocation = kmalloc(4096, GFP_KERNEL), *buf;
	int ret;

	if (!allocation)
		return -ENOMEM;
	memset(allocation, 0xa5, 4096);
	buf = allocation + 128;
	memcpy(chip->oob_poi, r->data + 2048, 128);
	ret = chip->ecc.read_subpage(chip, r->column, r->length, buf, r->page);
	memcpy(r->data, buf, 2048);
	memcpy(r->data + 2048, chip->oob_poi, 128);
	r->regs[14] = !memchr_inv(allocation, 0xa5, 128) &&
		!memchr_inv(allocation + 2176, 0xa5, 128);
	r->regs[15] = 0x11;
	if (!r->regs[14])
		ret = -EOVERFLOW;
	kfree(allocation);
	return ret;
}

/* Read-only NAND traffic. Allocations persist until module unload, including
 * throughout reset, delayed IRQ delivery, and the post-reset observation window.
 * A failed reset pins this module: power-cycle instead of releasing DMA memory.
 */
static int recovery_test(struct probe_request *r)
{
	static const u16 offsets[] = { CTL, 0x0c, 0x10, 0x14, 0x18, 0x1c, CNT,
		0x28, 0x2c, ECC_CTL, EFR, 0x70, 0x74, 0x78, 0x7c, SPARE };
	u32 saved[ARRAY_SIZE(offsets)], value;
	__le32 *desc = recovery_desc;
	u8 *buf = recovery_mem, *snapshot;
	unsigned int i;
	int ret, reset_ret;
	bool launched = false;

	if (!recovery_mem) {
		recovery_mem = dma_alloc_coherent(&pdev->dev, 4096, &recovery_dma, GFP_KERNEL);
		recovery_desc = dma_alloc_coherent(&pdev->dev, 64, &recovery_desc_dma, GFP_KERNEL);
		if (!recovery_mem || !recovery_desc) {
			if (recovery_mem)
				dma_free_coherent(&pdev->dev, 4096, recovery_mem, recovery_dma);
			if (recovery_desc)
				dma_free_coherent(&pdev->dev, 64, recovery_desc, recovery_desc_dma);
			recovery_mem = recovery_desc = NULL;
			return -ENOMEM;
		}
		buf = recovery_mem;
		desc = recovery_desc;
	}
	if (upper_32_bits(recovery_dma) || upper_32_bits(recovery_desc_dma))
		return -ERANGE;
	snapshot = kmalloc(4096, GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(offsets); i++)
		saved[i] = readl(regs + offsets[i]);
	memset(buf, 0xa5, 4096);
	memset(desc, 0, 64);
	desc[0] = cpu_to_le32(BIT(3) | BIT(2));
	desc[1] = cpu_to_le32(2048);
	desc[2] = cpu_to_le32(recovery_dma + 128);
	desc[3] = cpu_to_le32(recovery_desc_dma);
	ret = nand_read_page_op(chip, r->page, 0, NULL, 0);
	if (ret)
		goto restore_context;
	writel(0, regs + INT_EN);
	writel(BIT(0) | DONE | BIT(2), regs + ST);
	writel(1, regs + 0x204);
	writel((readl(regs + CTL) | BIT(14)) & ~BIT(15), regs + CTL);
	writel(3, regs + 0x1c);
	writel(1024, regs + CNT);
	writel(2048, regs + SPARE);
	writel(2, regs + USER_LEN);
	for (i = 1; i < 4; i++)
		writel(0, regs + USER_LEN + i * 4);
	writel((r->seed << 16) | (3 << 8) | BIT(5) | BIT(4) | BIT(3) | BIT(0), regs + ECC_CTL);
	writel((NAND_CMD_RNDOUTSTART << 16) | (NAND_CMD_RNDOUT << 8) | NAND_CMD_READSTART, regs + 0x28);
	dma_wmb();
	writel(recovery_desc_dma, regs + 0x200);
	writel(BIT(31) | READ_DATA, regs + CMD);
	launched = true;
	if (r->chunk)
		udelay(r->chunk);
	r->regs[0] = readl(regs + ST);
	r->regs[1] = readl(regs + 0x204);
	r->regs[2] = readl(regs + 0x214);
	if (r->flags == 0 || r->flags == 2) {
		ret = readl_poll_timeout(regs + ST, value,
			(value & (DONE | BIT(2))) == (DONE | BIT(2)) && !(value & BUSY), 1, 100000);
		r->regs[3] = value;
		if (!ret && r->flags == 2) {
			/* Deliver an already-latched event to the real driver's IRQ handler. */
			writel(DONE | BIT(2), regs + INT_EN);
			udelay(50);
			writel(0, regs + INT_EN);
			synchronize_irq(irq);
			r->regs[4] = readl(regs + ST);
			r->regs[5] = readl(regs + INT_EN);
		}
	}
	/* Reset even on ordinary completion so context restoration is exercised. */
	writel(0, regs + INT_EN);
	if (r->flags != 4)
		writel(0, regs + ECC_CTL);
	writel((r->flags == 3 || r->flags == 4 ? readl(regs + CTL) : 0) | BIT(1), regs + CTL);
	reset_ret = readl_poll_timeout(regs + CTL, value, !(value & BIT(1)), 1, 100000);
	if (r->flags == 4)
		writel(0, regs + ECC_CTL);
	r->regs[6] = value;
	r->regs[7] = readl(regs + ST);
	if (reset_ret) {
		poisoned = true;
		__module_get(THIS_MODULE);
		kfree(snapshot);
		pr_err("nfc_probe: reset failed; DMA buffers pinned; power cycle required\n");
		return reset_ret;
	}
	dma_rmb();
	memcpy(snapshot, buf, 4096);
	msleep(20);
	dma_rmb();
	r->regs[8] = !memcmp(snapshot, buf, 4096);
	msleep(80);
	dma_rmb();
	r->regs[9] = !memcmp(snapshot, buf, 4096);
	r->regs[10] = readl(regs + ST);
	r->regs[11] = readl(regs + 0x204);
	if (r->flags == 5) {
		/* Separate settled reset, retaining the same live allocations. */
		writel(BIT(1), regs + CTL);
		reset_ret = readl_poll_timeout(regs + CTL, value, !(value & BIT(1)), 1, 100000);
		if (reset_ret) {
			poisoned = true;
			__module_get(THIS_MODULE);
			kfree(snapshot);
			return reset_ret;
		}
	}
	r->regs[12] = 1;
	for (i = 0; i < 128; i++)
		if (buf[i] != 0xa5 || buf[2176 + i] != 0xa5)
			r->regs[12] = 0;
	memcpy(r->data, buf + 128, 2048);
restore_context:
	for (i = 1; i < ARRAY_SIZE(offsets); i++)
		writel(saved[i], regs + offsets[i]);
	writel(saved[0], regs + CTL);
	writel(BIT(0) | DONE | BIT(2), regs + ST);
	if (launched && !ret)
		ret = nand_reset_op(chip);
	kfree(snapshot);
	return ret;
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
	case PROBE_TRANSITIONS:
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
	case PROBE_CODEWORD_READ:
		ret = pio_ecc_read(r);
		break;
	case PROBE_NATIVE_READ ... PROBE_NATIVE_RAW_WRITE:
		ret = native_request(r);
		break;
	case PROBE_RECOVERY:
		ret = recovery_test(r);
		break;
	case PROBE_MASKED_NATIVE_READ:
		/* Real driver wait-for-completion expires; its status recheck runs. */
		disable_irq(irq);
		r->op = PROBE_NATIVE_READ;
		ret = native_request(r);
		r->op = PROBE_MASKED_NATIVE_READ;
		enable_irq(irq);
		break;
	case PROBE_ACTUAL_SUBREAD:
		ret = actual_subread(r);
		break;
	case PROBE_RECOVERY_SNAPSHOT:
		if (!recovery_mem) {
			ret = -ENODATA;
			break;
		}
		dma_rmb();
		memcpy(r->data, recovery_mem + 128, 2048);
		r->regs[0] = readl(regs + ST);
		r->regs[1] = readl(regs + 0x204);
		r->regs[2] = !memchr_inv(recovery_mem, 0xa5, 128) &&
			!memchr_inv(recovery_mem + 2176, 0xa5, 128);
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
	if (r->op == PROBE_ECC_READ || r->op == PROBE_ECC_WRITE ||
	    r->op == PROBE_ACTUAL_SUBREAD || r->op == PROBE_MASKED_NATIVE_READ ||
	    (r->op >= PROBE_NATIVE_READ && r->op <= PROBE_NATIVE_RAW_WRITE)) {
		r->regs[0] = master->ecc_stats.corrected - corrected;
		r->regs[1] = master->ecc_stats.failed - failed;
		r->regs[2] = readl(regs + ECC_ST);
		r->regs[3] = readl(regs + PAT_FOUND);
		r->regs[4] = readl(regs + PAT_ID);
		r->regs[5] = readl(regs + ERR_CNT);
		r->regs[6] = readl(regs + USER_LEN);
		for (unsigned int i = 1; i < 4; i++)
			r->regs[6 + i] = readl(regs + USER_LEN + i * 4);
	}
	memcpy(chip->oob_poi, saved_oob, sizeof(saved_oob));
	/* Synthetic errors must not change production MTD health statistics. */
	master->ecc_stats.corrected = corrected;
	master->ecc_stats.failed = failed;
	if (poisoned)
		goto unlock;
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

	if (allow_recovery) {
		ret = no_attached_ubi();
		if (ret)
			return ret;
	}

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
	if (recovery_mem)
		dma_free_coherent(&pdev->dev, 4096, recovery_mem, recovery_dma);
	if (recovery_desc)
		dma_free_coherent(&pdev->dev, 64, recovery_desc, recovery_desc_dma);
	iounmap(regs);
	put_device(&pdev->dev);
	put_mtd_device(partition);
}
module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Hilliard");
MODULE_DESCRIPTION("Restricted H616 NFC hardware-contract diagnostic");
