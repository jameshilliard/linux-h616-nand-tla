// SPDX-License-Identifier: GPL-2.0-or-later
/* Delete a child while its real MTD subpartition parser is blocked. */
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>

static DECLARE_COMPLETION(entered);
static DECLARE_COMPLETION(proceed);
static DECLARE_COMPLETION(finished);
static struct mtd_info master;
static struct device *parent;
static int child_index, register_result, parser_result;

static int test_read(struct mtd_info *mtd, loff_t from, size_t len,
		     size_t *retlen, u_char *buf)
{
	memset(buf, 0xff, len);
	*retlen = len;
	return 0;
}

static int parse_child(struct mtd_info *child, const struct mtd_partition **parts,
		       struct mtd_part_parser_data *data)
{
	static const struct mtd_partition subpart = {
		.name = "publication-grandchild", .offset = 0, .size = 0x1000,
	};
	int ret;

	child_index = child->index;
	complete(&entered);
	if (!wait_for_completion_timeout(&proceed, 10 * HZ))
		return -ETIMEDOUT;
	/* The caller, not the parser, must retain the child across this wait. */
	if (strcmp(child->name, "publication-child"))
		return -EINVAL;
	ret = mtd_add_partition(child, "publication-orphan", 0, 0x1000);
	if (ret != -ENODEV)
		return parser_result = -EINVAL;
	*parts = kmemdup(&subpart, sizeof(subpart), GFP_KERNEL);
	if (!*parts)
		return -ENOMEM;
	parser_result = 0;
	return 1;
}

static struct mtd_part_parser parser = {
	.owner = THIS_MODULE, .name = "publication-race", .parse_fn = parse_child,
};

static int register_worker(void *unused)
{
	static const char * const types[] = { "publication-race", NULL };
	static const struct mtd_partition part = {
		.name = "publication-child", .offset = 0, .size = 0x2000, .types = types,
	};

	register_result = mtd_device_register(&master, &part, 1);
	complete(&finished);
	while (!kthread_should_stop())
		schedule_timeout_interruptible(HZ);
	return 0;
}

static int __init test_init(void)
{
	struct task_struct *task;
	int ret;

	parent = root_device_register("mtd-publication-fixture");
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	master.name = "publication-master";
	master.owner = THIS_MODULE;
	master.parent_dev = parent;
	master.type = MTD_RAM;
	master.flags = MTD_NO_ERASE;
	master.size = 0x4000;
	master.erasesize = 0x1000;
	master.writesize = 1;
	master._read = test_read;
	ret = register_mtd_parser(&parser);
	if (ret)
		goto out_parent;
	parser_result = -EINPROGRESS;
	task = kthread_run(register_worker, NULL, "mtd-partition-race");
	if (IS_ERR(task)) {
		ret = PTR_ERR(task);
		goto out_parser;
	}
	if (!wait_for_completion_timeout(&entered, 10 * HZ))
		ret = -ETIMEDOUT;
	else
		ret = mtd_del_partition(&master, child_index);
	complete(&proceed);
	wait_for_completion(&finished);
	kthread_stop(task);
	if (!ret && (parser_result || register_result != -ENODEV))
		ret = -EINVAL;
	if (!register_result)
		mtd_device_unregister(&master);
	if (!ret)
		pr_info("MTD-PARTITION-RACE: parser survives deletion; orphan children rejected PASS\n");
out_parser:
	deregister_mtd_parser(&parser);
out_parent:
	root_device_unregister(parent);
	return ret;
}

static void __exit test_exit(void) { }
module_init(test_init);
module_exit(test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synthetic MTD partition publication/deletion regression");
