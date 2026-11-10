// SPDX-License-Identifier: GPL-2.0-or-later
/* The holder has no symbol dependency on the callback's owner module. */
#include <linux/completion.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "device_release_test.h"

static DEFINE_MUTEX(lock);
static DECLARE_COMPLETION(entered);
static DECLARE_COMPLETION(proceed);
static struct device *held;
static bool started;

int device_release_test_hold(struct device *dev)
{
	guard(mutex)(&lock);
	if (held || started)
		return -EBUSY;
	held = get_device(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(device_release_test_hold);

void device_release_test_gate(void)
{
	complete(&entered);
	if (!wait_for_completion_timeout(&proceed, 30 * HZ))
		pr_err("DEVICE-RELEASE-OWNER: FAIL (gate timeout)\n");
}
EXPORT_SYMBOL_GPL(device_release_test_gate);

static void release_work(struct work_struct *work)
{
	struct device *dev;

	scoped_guard(mutex, &lock) {
		dev = held;
		held = NULL;
	}
	put_device(dev);
}
static DECLARE_WORK(release, release_work);

static int command_set(const char *val, const struct kernel_param *kp)
{
	unsigned int command;
	int ret = kstrtouint(val, 0, &command);

	if (ret)
		return ret;
	if (command == 2) {
		complete(&proceed);
		return 0;
	}
	if (command != 1)
		return -EINVAL;
	guard(mutex)(&lock);
	if (!held || started)
		return -EINVAL;
	started = true;
	schedule_work(&release);
	return 0;
}

static int status_get(char *buffer, const struct kernel_param *kp)
{
	return sysfs_emit(buffer, "%u\n", completion_done(&entered));
}

static const struct kernel_param_ops command_ops = {
	.set = command_set,
	.get = status_get,
};
module_param_cb(command, &command_ops, NULL, 0600);

static int __init holder_init(void)
{
	return 0;
}

static void __exit holder_exit(void)
{
	complete(&proceed);
	flush_work(&release);
	WARN_ON(held);
}
module_init(holder_init);
module_exit(holder_exit);
MODULE_LICENSE("GPL");
