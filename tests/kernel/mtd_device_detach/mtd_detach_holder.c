// SPDX-License-Identifier: GPL-2.0-or-later
/* No dependency on MTD or on the provider module. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include "test.h"

static struct device *devices[3];
static struct nvmem_device *nvmem;
static unsigned int count;
static DEFINE_MUTEX(hold_lock);

int mtd_detach_hold_device(struct device *dev)
{
	guard(mutex)(&hold_lock);
	if (count == ARRAY_SIZE(devices))
		return -EBUSY;
	devices[count++] = get_device(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(mtd_detach_hold_device);

static int match_provider(struct device *dev, const void *data)
{
	return device_match_name(dev, "mtd2");
}

static int command_set(const char *val, const struct kernel_param *kp)
{
	unsigned int command, i;
	u8 byte;
	int ret = kstrtouint(val, 0, &command);

	if (ret)
		return ret;
	guard(mutex)(&hold_lock);
	switch (command) {
	case 1:
		if (count != 3 || nvmem)
			return -EINVAL;
		nvmem = nvmem_device_find(NULL, match_provider);
		if (IS_ERR(nvmem)) {
			ret = PTR_ERR(nvmem);
			nvmem = NULL;
			return ret;
		}
		return nvmem_device_read(nvmem, 0, 1, &byte) == 1 && byte == 0xa5 ? 0 : -EINVAL;
	case 2:
		if (!nvmem || nvmem_device_read(nvmem, 0, 1, &byte) != -ENODEV)
			return -EINVAL;
		fallthrough;
	case 4:
		if (count != 3)
			return -EINVAL;
		for (i = 0; i < count; i++) {
			if (device_is_registered(devices[i]) || dev_get_drvdata(devices[i]))
				return -EINVAL;
			pr_info("MTD-DETACH: retained device %s after core unload\n",
				dev_name(devices[i]));
		}
		return 0;
	case 3:
		if (count != 3)
			return -EINVAL;
		if (nvmem)
			nvmem_device_put(nvmem);
		nvmem = NULL;
		/* Parent-first puts catch reliance on deleted kobject parent links. */
		for (i = 0; i < count; i++) {
			put_device(devices[i]);
			devices[i] = NULL;
		}
		count = 0;
		pr_info("MTD-DETACH: final device puts after core unload\n");
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct kernel_param_ops command_ops = { .set = command_set };
module_param_cb(command, &command_ops, NULL, 0200);

static int __init holder_init(void)
{
	return 0;
}

static void __exit holder_exit(void)
{
	unsigned int i;

	if (nvmem)
		nvmem_device_put(nvmem);
	for (i = 0; i < count; i++)
		put_device(devices[i]);
}
module_init(holder_init);
module_exit(holder_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Retain MTD class devices across MTD module unload");
