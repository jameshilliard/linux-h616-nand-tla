// SPDX-License-Identifier: GPL-2.0-or-later
/* This module has no symbol dependency on the provider being unloaded. */
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "test.h"

static struct device *consumer, *held_device;
static struct nvmem_device *held;
static struct nvmem_cell *held_cell;
static unsigned int phase;
static unsigned int adds, removes;
module_param(adds, uint, 0400);
module_param(removes, uint, 0400);
static int worker_result;
static DECLARE_COMPLETION(entered);
static DECLARE_COMPLETION(proceed);
static DECLARE_COMPLETION(finished);
module_param(phase, uint, 0600);

static int wp_value;

static int wp_direction(struct gpio_chip *chip, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int wp_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	wp_value = value;
	return 0;
}

static int wp_get(struct gpio_chip *chip, unsigned int offset)
{
	return wp_value;
}

static struct gpio_chip wp_chip = {
	.label = "nvmem-detach-wp", .owner = THIS_MODULE, .base = -1, .ngpio = 1,
	.get_direction = wp_direction, .direction_output = wp_set,
	.get = wp_get, .set = wp_set,
};

static struct gpiod_lookup_table wp_lookup = {
	.dev_id = "nvmem-detach-provider",
	.table = { GPIO_LOOKUP("nvmem-detach-wp", 0, "wp", GPIO_ACTIVE_HIGH), {} },
};

static struct nvmem_cell_lookup lookup = {
	.nvmem_name = "nvmem-detach-fixture", .cell_name = "token",
	.dev_id = "nvmem-detach-consumer", .con_id = "token",
};

static int test_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	if (action == NVMEM_ADD)
		adds++;
	else if (action == NVMEM_REMOVE)
		removes++;
	return NOTIFY_OK;
}

static struct notifier_block test_notifier = { .notifier_call = test_notify };

static bool explicit_owner;
static int owner_probe_result;

static int owner_read(void *priv, unsigned int offset, void *buf, size_t len)
{
	memset(buf, 0, len);
	return 0;
}

static int owner_probe(struct platform_device *pdev)
{
	struct nvmem_config config = {
		.dev = &pdev->dev, .name = "nvmem-owner-fixture", .id = NVMEM_DEVID_NONE,
		.size = 1, .reg_read = owner_read, .owner_is_explicit = explicit_owner,
		.ignore_wp = true,
	};
	struct nvmem_device *provider, *consumer;
	int before, after;

	provider = nvmem_register(&config);
	if (IS_ERR(provider))
		return owner_probe_result = PTR_ERR(provider);
	before = module_refcount(THIS_MODULE);
	consumer = nvmem_device_get(&pdev->dev, config.name);
	after = module_refcount(THIS_MODULE);
	owner_probe_result = IS_ERR(consumer) ? PTR_ERR(consumer) :
		(after == before + !explicit_owner ? 0 : -EINVAL);
	if (!IS_ERR(consumer))
		nvmem_device_put(consumer);
	nvmem_unregister(provider);
	return owner_probe_result;
}

static struct platform_driver owner_driver = {
	.driver.name = "nvmem-owner-fixture",
	.probe = owner_probe,
};

static int test_owner(void)
{
	struct platform_device *pdev;
	int ret = platform_driver_register(&owner_driver);

	if (ret)
		return ret;
	for (int i = 0; i < 2; i++) {
		explicit_owner = i;
		owner_probe_result = -ENXIO;
		pdev = platform_device_register_simple("nvmem-owner-fixture", -1, NULL, 0);
		if (IS_ERR(pdev)) {
			ret = PTR_ERR(pdev);
			break;
		}
		ret = owner_probe_result;
		platform_device_unregister(pdev);
		if (ret)
			break;
	}
	platform_driver_unregister(&owner_driver);
	if (!ret)
		pr_info("NVMEM-DETACH: explicit NULL owner and legacy fallback PASS\n");
	return ret;
}

int nvmem_detach_test_gate(unsigned int which)
{
	if (READ_ONCE(phase) != which)
		return 0;
	complete(&entered);
	if (!wait_for_completion_timeout(&proceed, 15 * HZ))
		return -ETIMEDOUT;
	return 0;
}
EXPORT_SYMBOL_GPL(nvmem_detach_test_gate);

static int match_provider(struct device *dev, const void *data)
{
	if (!device_match_name(dev, "nvmem-detach-fixture"))
		return 0;
	held_device = get_device(dev);
	return 1;
}

static void read_work(struct work_struct *work)
{
	u8 byte = 0xa5;
	void *buf;
	size_t len;

	if (phase == 3) {
		buf = nvmem_cell_read(held_cell, &len);
		worker_result = IS_ERR(buf) ? PTR_ERR(buf) : len;
		if (!IS_ERR(buf))
			kfree(buf);
	} else if (phase == 2) {
		worker_result = nvmem_device_write(held, 0, 1, &byte);
	} else {
		worker_result = nvmem_device_read(held, 0, 1, &byte);
	}
	complete(&finished);
}
static DECLARE_WORK(reader, read_work);

static int command_set(const char *val, const struct kernel_param *kp)
{
	struct nvmem_device *late;
	struct nvmem_cell *new_cell;
	unsigned int command;
	u8 byte = 0xa5;
	void *buf;
	size_t len;
	int ret = kstrtouint(val, 0, &command);

	if (ret)
		return ret;
	switch (command) {
	case 1:
		if (held || held_device)
			return -EBUSY;
		held = nvmem_device_find(NULL, match_provider);
		if (IS_ERR(held)) {
			ret = PTR_ERR(held);
			held = NULL;
			return ret;
		}
		held_cell = nvmem_cell_get(consumer, "token");
		if (IS_ERR(held_cell)) {
			ret = PTR_ERR(held_cell);
			held_cell = NULL;
			return ret;
		}
		if (nvmem_device_read(held, 0, 1, &byte) != 1 || byte != 0xa5 ||
		    nvmem_device_write(held, 0, 1, &byte) != 1)
			return -EINVAL;
		return 0;
	case 2:
		if (!held || !held_cell || phase < 1 || phase > 3)
			return -EINVAL;
		schedule_work(&reader);
		return 0;
	case 3:
		complete(&proceed);
		return 0;
	case 4:
		if (!held || !held_cell || !completion_done(&finished) ||
		    worker_result != 1)
			return -EINVAL;
		if (nvmem_device_read(held, 0, 1, &byte) != -ENODEV ||
		    nvmem_device_write(held, 0, 1, &byte) != -ENODEV)
			return -EINVAL;
		buf = nvmem_cell_read(held_cell, &len);
		if (!IS_ERR(buf)) {
			kfree(buf);
			return -EINVAL;
		}
		if (PTR_ERR(buf) != -ENODEV)
			return -EINVAL;
		late = nvmem_device_get(consumer, "nvmem-detach-fixture");
		if (!IS_ERR(late)) {
			nvmem_device_put(late);
			return -EINVAL;
		}
		nvmem_cell_put(held_cell);
		held_cell = NULL;
		nvmem_device_put(held);
		held = NULL;
		pr_info("NVMEM-DETACH: stale consumers rejected after unload\n");
		return 0;
	case 5:
		if (held || held_cell || !held_device)
			return -EINVAL;
		put_device(held_device);
		held_device = NULL;
		pr_info("NVMEM-DETACH: final bare device put after provider unload\n");
		return 0;
	case 6:
		if (nvmem_device_read(held, 0, 1, &byte) != 1)
			return -EINVAL;
		buf = nvmem_cell_read(held_cell, &len);
		if (!IS_ERR(buf)) {
			kfree(buf);
			return -EINVAL;
		}
		return PTR_ERR(buf) == -ENODEV ? 0 : -EINVAL;
	case 7:
		new_cell = nvmem_cell_get(consumer, "token");
		if (IS_ERR(new_cell))
			return PTR_ERR(new_cell);
		buf = nvmem_cell_read(new_cell, &len);
		ret = IS_ERR(buf) ? PTR_ERR(buf) : 0;
		if (!IS_ERR(buf))
			kfree(buf);
		nvmem_cell_put(new_cell);
		return ret;
	case 8:
		if (!held || !held_cell)
			return -EINVAL;
		nvmem_cell_put(held_cell);
		held_cell = NULL;
		nvmem_device_put(held);
		held = NULL;
		return 0;
	default:
		return -EINVAL;
	}
}

static int status_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%u\n", completion_done(&entered));
}

static const struct kernel_param_ops command_ops = {
	.set = command_set, .get = status_get,
};
module_param_cb(command, &command_ops, NULL, 0600);

static int __init holder_init(void)
{
	int ret;

	ret = test_owner();
	if (ret)
		return ret;
	consumer = root_device_register("nvmem-detach-consumer");
	if (IS_ERR(consumer))
		return PTR_ERR(consumer);
	wp_chip.parent = consumer;
	ret = gpiochip_add_data(&wp_chip, NULL);
	if (ret) {
		root_device_unregister(consumer);
		return ret;
	}
	gpiod_add_lookup_table(&wp_lookup);
	nvmem_add_cell_lookups(&lookup, 1);
	nvmem_register_notifier(&test_notifier);
	return 0;
}

static void __exit holder_exit(void)
{
	nvmem_unregister_notifier(&test_notifier);
	complete(&proceed);
	flush_work(&reader);
	if (held_cell)
		nvmem_cell_put(held_cell);
	if (held)
		nvmem_device_put(held);
	put_device(held_device);
	nvmem_del_cell_lookups(&lookup, 1);
	gpiod_remove_lookup_table(&wp_lookup);
	gpiochip_remove(&wp_chip);
	root_device_unregister(consumer);
}

module_init(holder_init);
module_exit(holder_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Retained NVMEM consumer and callback-drain test");
