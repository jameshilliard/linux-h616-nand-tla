// SPDX-License-Identifier: GPL-2.0-or-later
/* Synthetic controller: exercises real NAND scan, I/O and devres teardown. */
#include <linux/module.h>
#include <linux/mtd/rawnand.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define TEST_NAME "raw-nand-lifetime-test"
#define TEST_CHIPS 2

struct test_controller {
	struct nand_controller controller;
	struct nand_chip *chips[TEST_CHIPS];
	bool hardware_live;
	u8 command, address;
};

static unsigned int chip_releases, controller_releases, invalid_accesses;
static struct test_controller *current_controller;

static int test_exec(struct nand_chip *chip, const struct nand_operation *op,
		     bool check_only)
{
	struct test_controller *ctrl = container_of(chip->controller,
						  struct test_controller, controller);
	static const u8 id[] = { 0x7f, 0xa1, 0, 0, 0, 0, 0, 0 };
	unsigned int i;

	if (check_only)
		return 0;
	if (!ctrl->hardware_live) {
		invalid_accesses++;
		return -ENODEV;
	}
	for (i = 0; i < op->ninstrs; i++) {
		const struct nand_op_instr *ins = &op->instrs[i];
		u8 *buf;

		switch (ins->type) {
		case NAND_OP_CMD_INSTR:
			ctrl->command = ins->ctx.cmd.opcode;
			break;
		case NAND_OP_ADDR_INSTR:
			ctrl->address = ins->ctx.addr.addrs[0];
			break;
		case NAND_OP_DATA_IN_INSTR:
			buf = ins->ctx.data.buf.in;
			memset(buf, 0xff, ins->ctx.data.len);
			if (ctrl->command == NAND_CMD_READID) {
				memset(buf, 0, ins->ctx.data.len);
				if (!ctrl->address)
					memcpy(buf, id, min_t(size_t, sizeof(id), ins->ctx.data.len));
			} else if (ctrl->command == NAND_CMD_STATUS) {
				memset(buf, NAND_STATUS_READY | NAND_STATUS_WP, ins->ctx.data.len);
			}
			break;
		default:
			break;
		}
	}
	return 0;
}

static const struct nand_controller_ops test_ops = {
	.exec_op = test_exec,
};

static void test_chip_release(struct nand_chip *chip)
{
	chip_releases++;
	kfree(chip);
}

static void test_controller_release(struct nand_controller *controller)
{
	controller_releases++;
	kfree(container_of(controller, struct test_controller, controller));
}

static void test_remove(struct platform_device *pdev)
{
	struct test_controller *ctrl = platform_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < TEST_CHIPS; i++) {
		struct nand_chip *chip = ctrl->chips[i];

		if (!chip)
			continue;
		WARN_ON(mtd_device_unregister(nand_to_mtd(chip)));
		nand_disconnect(chip);
		ctrl->chips[i] = NULL;
		mtd_put_master(nand_to_mtd(chip));
	}
	ctrl->hardware_live = false;
}

static int test_probe(struct platform_device *pdev)
{
	static struct nand_flash_dev ids[] = {
		{ .name = "synthetic NAND", .id = { 0x7f, 0xa1 }, .id_len = 2,
		  .pagesize = 512, .chipsize = 2, .erasesize = 16384, .oobsize = 16 },
		{ }
	};
	struct test_controller *ctrl;
	unsigned int i;
	int ret;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;
	ret = devm_nand_controller_init(&pdev->dev, &ctrl->controller, test_controller_release);
	if (ret)
		return ret;
	ctrl->controller.ops = &test_ops;
	ctrl->hardware_live = true;
	platform_set_drvdata(pdev, ctrl);
	for (i = 0; i < TEST_CHIPS; i++) {
		struct nand_chip *chip = kzalloc(sizeof(*chip), GFP_KERNEL);
		struct mtd_info *mtd;

		if (!chip) {
			ret = -ENOMEM;
			goto fail;
		}
		chip->controller = &ctrl->controller;
		chip->options = NAND_SKIP_BBTSCAN;
		chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_NONE;
		mtd = nand_to_mtd(chip);
		mtd->name = "synthetic NAND";
		mtd->owner = THIS_MODULE;
		mtd->parent_dev = &pdev->dev;
		ret = nand_scan_with_ids(chip, 1, ids);
		if (ret) {
			kfree(chip);
			goto fail;
		}
		ret = nand_set_release(chip, test_chip_release);
		if (ret) {
			nand_cleanup(chip);
			kfree(chip);
			goto fail;
		}
		ret = mtd_device_register(mtd, NULL, 0);
		if (ret) {
			nand_disconnect(chip);
			mtd_put_master(mtd);
			goto fail;
		}
		ctrl->chips[i] = chip;
	}
	current_controller = ctrl;
	return 0;
fail:
	test_remove(pdev);
	return ret;
}

static struct platform_driver test_driver = {
	.driver.name = TEST_NAME,
	.probe = test_probe,
	.remove = test_remove,
};

static int test_run(const char *val, const struct kernel_param *kp)
{
	struct platform_device *old, *replacement;
	struct mtd_info *handles[TEST_CHIPS], *new_mtd;
	struct device *device;
	unsigned int before = chip_releases, ctrl_before = controller_releases, i;
	size_t retlen = 0;
	u8 byte;
	int ret = 0;

	current_controller = NULL;
	old = platform_device_register_simple(TEST_NAME, PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(old))
		return PTR_ERR(old);
	if (!current_controller) {
		platform_device_unregister(old);
		return -ENODEV;
	}
	for (i = 0; i < TEST_CHIPS; i++)
		handles[i] = get_mtd_device(nand_to_mtd(current_controller->chips[i]), -1);
	for (i = 0; i < TEST_CHIPS; i++) {
		if (IS_ERR(handles[i])) {
			ret = PTR_ERR(handles[i]);
			goto out_old;
		}
	}
	device = get_device(handles[0]->dev);
	platform_device_unregister(old);
	if (chip_releases != before || controller_releases != ctrl_before)
		ret = -EINVAL;
	for (i = 0; i < TEST_CHIPS; i++) {
		if (mtd_read(handles[i], 0, 1, &retlen, &byte) != -ENODEV)
			ret = -EINVAL;
		mtd_sync(handles[i]);
	}
	current_controller = NULL;
	replacement = platform_device_register_simple(TEST_NAME, PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(replacement)) {
		ret = PTR_ERR(replacement);
	} else {
		if (!current_controller) {
			ret = -ENODEV;
		} else {
			new_mtd = nand_to_mtd(current_controller->chips[0]);
			if (new_mtd->index == handles[0]->index ||
			    mtd_read(new_mtd, 0, 1, &retlen, &byte) || byte != 0xff)
				ret = -EINVAL;
			if (mtd_read(handles[0], 0, 1, &retlen, &byte) != -ENODEV)
				ret = -EINVAL;
		}
		platform_device_unregister(replacement);
	}
	for (i = 0; i < TEST_CHIPS; i++)
		put_mtd_device(handles[i]);
	if (!ret && (chip_releases != before + 4 || controller_releases != ctrl_before + 2))
		ret = -EINVAL;
	new_mtd = dev_get_drvdata(device);
	if (new_mtd)
		ret = -EINVAL;
	put_device(device);
	if (!ret && (chip_releases != before + 4 || controller_releases != ctrl_before + 2))
		ret = -EINVAL;
	if (invalid_accesses)
		ret = -EINVAL;
	pr_info("raw-nand-lifetime: %s (%u chips, %u controllers)\n",
		ret ? "FAIL" : "PASS", chip_releases, controller_releases);
	return ret;
out_old:
	platform_device_unregister(old);
	for (i = 0; i < TEST_CHIPS; i++)
		if (!IS_ERR(handles[i]))
			put_mtd_device(handles[i]);
	return ret;
}

static const struct kernel_param_ops run_ops = { .set = test_run };
module_param_cb(run, &run_ops, NULL, 0200);
module_platform_driver(test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synthetic raw NAND retained-handle and rebind checks");
