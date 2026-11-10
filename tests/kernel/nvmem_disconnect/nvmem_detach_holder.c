// SPDX-License-Identifier: GPL-2.0-or-later
/* This module has no symbol dependency on the provider being unloaded. */
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "test.h"

static struct device *consumer, *held_device;
static struct nvmem_device *held;
static struct nvmem_cell *held_cell;
static unsigned int phase;
static int worker_result;
static DECLARE_COMPLETION(entered);
static DECLARE_COMPLETION(proceed);
static DECLARE_COMPLETION(finished);
module_param(phase, uint, 0600);

static struct nvmem_cell_lookup lookup = {
	.nvmem_name = "nvmem-detach-fixture", .cell_name = "token",
	.dev_id = "nvmem-detach-consumer", .con_id = "token",
};

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
	consumer = root_device_register("nvmem-detach-consumer");
	if (IS_ERR(consumer))
		return PTR_ERR(consumer);
	nvmem_add_cell_lookups(&lookup, 1);
	return 0;
}

static void __exit holder_exit(void)
{
	complete(&proceed);
	flush_work(&reader);
	if (held_cell)
		nvmem_cell_put(held_cell);
	if (held)
		nvmem_device_put(held);
	put_device(held_device);
	nvmem_del_cell_lookups(&lookup, 1);
	root_device_unregister(consumer);
}

module_init(holder_init);
module_exit(holder_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Retained NVMEM consumer and callback-drain test");
