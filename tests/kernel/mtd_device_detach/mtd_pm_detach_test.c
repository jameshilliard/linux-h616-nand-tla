// SPDX-License-Identifier: GPL-2.0-or-later
/* Exercise the PM core's device-lock callback protocol during device_del(). */
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/sched/task.h>
#include <linux/slab.h>

static struct mtd_info mtd;
static struct device *dev;
static DECLARE_COMPLETION(start_pm);
static DECLARE_COMPLETION(pm_entered);
static DECLARE_COMPLETION(finish_pm);
static DECLARE_COMPLETION(removed);
static bool released;
static int pm_ret, remove_ret, hook_ret;

static int suspend_mtd(struct mtd_info *unused)
{
	complete_all(&pm_entered);
	if (!wait_for_completion_timeout(&finish_pm, 5 * HZ))
		return -ETIMEDOUT;
	return READ_ONCE(released) ? -EFAULT : 0;
}

static void release_mtd(struct mtd_info *unused)
{
	/* Keep storage to report a missed drain without dereferencing freed data. */
	WRITE_ONCE(released, true);
}

static void remove_device(struct device *removed_dev)
{
	if (removed_dev != dev)
		return;
	/* device_del() has released its initial device_lock() at this point. */
	complete(&start_pm);
	if (!wait_for_completion_timeout(&pm_entered, 5 * HZ))
		hook_ret = -ETIMEDOUT;
}

static struct class_interface interface = {
	.remove_dev = remove_device,
};

static int pm_thread(void *unused)
{
	if (!wait_for_completion_timeout(&start_pm, 5 * HZ)) {
		pm_ret = -ETIMEDOUT;
		return 0;
	}
	/* Match device_suspend(): select and invoke class PM under device_lock(). */
	device_lock(dev);
	if (dev->class && dev->class->pm && dev->class->pm->suspend)
		pm_ret = dev->class->pm->suspend(dev);
	else
		pm_ret = -ENODEV;
	device_unlock(dev);
	return 0;
}

static int remove_thread(void *unused)
{
	remove_ret = mtd_device_unregister(&mtd);
	mtd_put_master(&mtd);
	complete(&removed);
	return 0;
}

static int __init pm_test_init(void)
{
	struct task_struct *pm_task, *remove_task;
	int ret;

	mtd.name = "pm-detach-fixture";
	mtd.owner = THIS_MODULE;
	mtd.type = MTD_RAM;
	mtd.flags = MTD_NO_ERASE;
	mtd.size = 0x1000;
	mtd.erasesize = 0x1000;
	mtd.writesize = 1;
	mtd._suspend = suspend_mtd;
	mtd_set_release(&mtd, release_mtd);
	ret = mtd_device_register(&mtd, NULL, 0);
	if (ret) {
		mtd_put_master(&mtd);
		return ret;
	}
	dev = get_device(mtd.dev);
	interface.class = dev->class;
	ret = class_interface_register(&interface);
	if (ret)
		goto unregister;
	pm_task = kthread_create(pm_thread, NULL, "mtd-detach-pm");
	if (IS_ERR(pm_task)) {
		ret = PTR_ERR(pm_task);
		goto unregister_interface;
	}
	get_task_struct(pm_task);
	remove_task = kthread_create(remove_thread, NULL, "mtd-detach-remove");
	if (IS_ERR(remove_task)) {
		ret = PTR_ERR(remove_task);
		kthread_stop(pm_task);
		put_task_struct(pm_task);
		goto unregister_interface;
	}
	get_task_struct(remove_task);
	wake_up_process(pm_task);
	wake_up_process(remove_task);
	ret = 0;
	if (!wait_for_completion_timeout(&pm_entered, 5 * HZ))
		ret = -ETIMEDOUT;
	else if (wait_for_completion_timeout(&removed, HZ / 10)) {
		pr_err("MTD-PM-DETACH: FAIL removal passed an active PM callback\n");
		ret = -EFAULT;
	}
	complete(&finish_pm);
	kthread_stop(pm_task);
	kthread_stop(remove_task);
	put_task_struct(pm_task);
	put_task_struct(remove_task);
	if (pm_ret || remove_ret || hook_ret || !released)
		ret = -EFAULT;
	device_lock(dev);
	if (dev_get_drvdata(dev) || dev->groups)
		ret = -EFAULT;
	if (!dev->class || !dev->class->pm || !dev->type ||
	    strcmp(dev->type->name, "mtd") || dev->class->pm->suspend(dev) ||
	    dev->class->pm->resume(dev))
		ret = -EFAULT;
	device_unlock(dev);
	class_interface_unregister(&interface);
	put_device(dev);
	if (!ret)
		pr_info("MTD-PM-DETACH: active callback drained and later callbacks blocked PASS\n");
	return ret;

unregister_interface:
	class_interface_unregister(&interface);
unregister:
	mtd_device_unregister(&mtd);
	mtd_put_master(&mtd);
	put_device(dev);
	return ret;
}

static void __exit pm_test_exit(void)
{
}
module_init(pm_test_init);
module_exit(pm_test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Race MTD class PM callbacks with metadata detachment");
