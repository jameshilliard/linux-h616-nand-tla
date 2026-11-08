// SPDX-License-Identifier: GPL-2.0-only
/* Hold real-board NVMEM providers through unbind and replacement probing. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>

static char *native_name;
static char *boot_name;
module_param(native_name, charp, 0444);
module_param(boot_name, charp, 0444);
static struct device *parent;
static struct nvmem_device *held[2];

static int check_removed(const char *val, const struct kernel_param *kp)
{
	bool removed;
	u8 buf[16];
	int i, ret;

	ret = kstrtobool(val, &removed);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(held); i++) {
		ret = nvmem_device_read(held[i], 0, sizeof(buf), buf);
		pr_info("nand-nvmem-hold: provider=%s removed=%d ret=%d\n",
			nvmem_dev_name(held[i]), removed, ret);
		if (ret != (removed ? -ENODEV : sizeof(buf)))
			return -EINVAL;
	}
	return 0;
}

static const struct kernel_param_ops check_ops = { .set = check_removed };
module_param_cb(check, &check_ops, NULL, 0200);

static int __init hold_init(void)
{
	int ret;

	if (!native_name || !boot_name)
		return -EINVAL;
	parent = root_device_register("nand-nvmem-hold");
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	held[0] = nvmem_device_get(parent, native_name);
	if (IS_ERR(held[0])) {
		ret = PTR_ERR(held[0]);
		goto free_parent;
	}
	held[1] = nvmem_device_get(parent, boot_name);
	if (IS_ERR(held[1])) {
		ret = PTR_ERR(held[1]);
		goto put_native;
	}
	ret = check_removed("N", NULL);
	if (!ret)
		return 0;
	nvmem_device_put(held[1]);
put_native:
	nvmem_device_put(held[0]);
free_parent:
	root_device_unregister(parent);
	return ret;
}

static void __exit hold_exit(void)
{
	nvmem_device_put(held[1]);
	nvmem_device_put(held[0]);
	root_device_unregister(parent);
	pr_info("nand-nvmem-hold: both provider references released\n");
}
module_init(hold_init);
module_exit(hold_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only NAND provider lifetime test consumer");
