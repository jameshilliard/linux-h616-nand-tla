// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/slab.h>

#include "device_release_test.h"

static bool prepared;

static void owner_release(struct device *dev)
{
	if (dev_get_drvdata(dev)) {
		WARN_ON(module_refcount(THIS_MODULE) < 1);
		device_release_test_gate();
		WARN_ON(module_refcount(THIS_MODULE) < 1);
		pr_info("DEVICE-RELEASE-OWNER: callback still pinned\n");
	}
	kfree(dev);
}

/* Called after module initialization, so failure can safely defer release. */
static int prepare_set(const char *val, const struct kernel_param *kp)
{
	struct device *dev;
	unsigned int mode;
	bool added = false;
	int ret;

	ret = kstrtouint(val, 0, &mode);
	if (ret)
		return ret;
	if (prepared || mode > 2)
		return -EINVAL;
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->release = owner_release;
	dev->release_owner = THIS_MODULE;
	device_initialize(dev);
	if (mode == 1) {
		ret = dev_set_name(dev, "release-owner-fixture");
		if (ret)
			goto out;
		ret = device_add(dev);
		if (ret)
			goto out;
		added = true;
	} else if (mode == 2) {
		/* No name: device_add() must fail after initialization. */
		ret = device_add(dev);
		if (ret != -EINVAL) {
			added = !ret;
			ret = -EINVAL;
			goto out;
		}
	}
	ret = device_release_test_hold(dev);
	if (!ret) {
		prepared = true;
		dev_set_drvdata(dev, dev);
	}
out:
	if (added)
		device_del(dev);
	put_device(dev);
	return ret;
}

static const struct kernel_param_ops prepare_ops = {
	.set = prepare_set,
};
module_param_cb(prepare, &prepare_ops, NULL, 0200);

static int __init owner_init(void)
{
	return 0;
}

static void __exit owner_exit(void)
{
	pr_info("DEVICE-RELEASE-OWNER: module exit after release\n");
}
module_init(owner_init);
module_exit(owner_exit);
MODULE_LICENSE("GPL");
