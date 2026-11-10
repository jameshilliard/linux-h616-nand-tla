// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "test.h"

struct test_mtd {
	struct mtd_info mtd;
	struct mutex lock;
	bool dead;
};

static struct test_mtd *test;
static struct device *parent;
static unsigned int releases;
static bool fail_init, retain_devices = true;
module_param(fail_init, bool, 0400);
module_param(retain_devices, bool, 0400);

static int read_data(struct mtd_info *mtd, loff_t from, size_t len,
		     size_t *retlen, u_char *buf)
{
	struct test_mtd *obj = container_of(mtd, struct test_mtd, mtd);

	guard(mutex)(&obj->lock);
	if (obj->dead)
		return -ENODEV;
	memset(buf, 0xa5, len);
	*retlen = len;
	return 0;
}

static void release_data(struct mtd_info *mtd)
{
	struct test_mtd *obj = container_of(mtd, struct test_mtd, mtd);

	releases++;
	mutex_destroy(&obj->lock);
	kfree(obj);
	pr_info("MTD-DETACH: provider metadata freed\n");
}

static void disconnect(void)
{
	struct test_mtd *obj = test;

	if (!obj)
		return;
	test = NULL;
	scoped_guard(mutex, &obj->lock)
		obj->dead = true;
	WARN_ON(mtd_device_unregister(&obj->mtd));
	mtd_put_master(&obj->mtd);
}

static int command_set(const char *val, const struct kernel_param *kp)
{
	unsigned int command;
	int ret = kstrtouint(val, 0, &command);

	if (ret || command != 1)
		return -EINVAL;
	disconnect();
	return 0;
}
static const struct kernel_param_ops command_ops = { .set = command_set };
module_param_cb(command, &command_ops, NULL, 0200);

static int __init provider_init(void)
{
	struct mtd_info *shelf, *leaf;
	int ret;

	parent = root_device_register("mtd-detach-fixture");
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	test = kzalloc(sizeof(*test), GFP_KERNEL);
	if (!test) {
		ret = -ENOMEM;
		goto fail_parent;
	}
	mutex_init(&test->lock);
	test->mtd.name = "detach-master";
	test->mtd.owner = THIS_MODULE;
	test->mtd.parent_dev = parent;
	test->mtd.type = MTD_RAM;
	test->mtd.flags = MTD_NO_ERASE;
	test->mtd.size = 0x40000;
	test->mtd.erasesize = 0x1000;
	test->mtd.writesize = 1;
	test->mtd._read = read_data;
	mtd_set_release(&test->mtd, release_data);
	ret = mtd_device_register(&test->mtd, NULL, 0);
	if (ret) {
		mtd_put_master(&test->mtd);
		test = NULL;
		goto fail_parent;
	}
	ret = mtd_add_partition(&test->mtd, "detach-shelf", 0, 0x20000);
	if (ret)
		goto fail_registered;
	shelf = get_mtd_device_nm("detach-shelf");
	if (IS_ERR(shelf)) {
		ret = PTR_ERR(shelf);
		goto fail_registered;
	}
	ret = mtd_add_partition(shelf, "detach-leaf", 0, 0x10000);
	if (ret)
		goto fail_shelf;
	leaf = get_mtd_device_nm("detach-leaf");
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		goto fail_shelf;
	}
	if (retain_devices) {
		ret = mtd_detach_hold_device(test->mtd.dev);
		if (!ret)
			ret = mtd_detach_hold_device(shelf->dev);
		if (!ret)
			ret = mtd_detach_hold_device(leaf->dev);
	}
	if (!ret && fail_init)
		ret = -ECANCELED;
	put_mtd_device(leaf);
fail_shelf:
	put_mtd_device(shelf);
	if (!ret)
		return 0;
fail_registered:
	disconnect();
fail_parent:
	root_device_unregister(parent);
	return ret;
}

static void __exit provider_exit(void)
{
	disconnect();
	WARN_ON(releases != 1);
	root_device_unregister(parent);
	pr_info("MTD-DETACH: provider unloaded\n");
}
module_init(provider_init);
module_exit(provider_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synthetic nested MTD partitions for detached-device testing");
