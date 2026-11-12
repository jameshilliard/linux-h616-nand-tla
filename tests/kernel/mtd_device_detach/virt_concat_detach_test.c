// SPDX-License-Identifier: GPL-2.0-or-later
/* Synthetic masters and a private DT fixture; no flash access. */
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_fdt.h>
#include <linux/sched/task.h>
#include <linux/sizes.h>
#include <linux/slab.h>

extern unsigned char concat_fixture_dtb[];
extern unsigned int concat_fixture_dtb_len;

struct fixture_master {
	struct mtd_info mtd;
	struct mutex lock;
	bool disconnected;
	u8 value;
};

static struct device *parent;
static struct device_node *nodes[2];
static atomic_t releases = ATOMIC_INIT(0);
static atomic_t layout_calls = ATOMIC_INIT(0);
static bool unmanaged;
static unsigned int layout_mask;

static int fixture_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	struct fixture_master *master = container_of(mtd, struct fixture_master, mtd);

	atomic_inc(&layout_calls);
	if (section)
		return -ERANGE;
	region->offset = master->value & 3;
	region->length = 4;
	return 0;
}

static int fixture_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	int ret = fixture_ooblayout_free(mtd, section, region);

	if (!ret) {
		region->offset += 4;
		region->length = 8;
	}
	return ret;
}

static const struct mtd_ooblayout_ops fixture_ooblayout = {
	.ecc = fixture_ooblayout_ecc,
	.free = fixture_ooblayout_free,
};

static int fixture_read(struct mtd_info *mtd, loff_t off, size_t len,
			size_t *retlen, u_char *buf)
{
	struct fixture_master *master = container_of(mtd, struct fixture_master, mtd);

	guard(mutex)(&master->lock);
	if (master->disconnected)
		return -ENODEV;
	memset(buf, master->value, len);
	*retlen = len;
	return 0;
}

static void fixture_release(struct mtd_info *mtd)
{
	atomic_inc(&releases);
	kfree(container_of(mtd, struct fixture_master, mtd));
}

static void fixture_put(struct mtd_info *mtd)
{
	if (mtd->master.release)
		mtd_put_master(mtd);
	else
		fixture_release(mtd);
}

static int fail_parse(struct mtd_info *mtd, const struct mtd_partition **parts,
		      struct mtd_part_parser_data *data)
{
	return -EIO;
}

static struct mtd_part_parser fail_parser = {
	.owner = THIS_MODULE,
	.name = "concat-lifetime-fail",
	.parse_fn = fail_parse,
};

static struct fixture_master *fixture_register(unsigned int id, u8 value,
					       bool incompatible, bool fail)
{
	static const char * const fail_types[] = { "concat-lifetime-fail", NULL };
	struct mtd_partition parts[] = {
		{ .name = id ? "concat-b" : "concat-a", .size = SZ_4K,
		  .of_node = nodes[id] },
		{ .name = id ? "concat-after-b" : "concat-after-a", .offset = MTDPART_OFS_APPEND,
		  .size = SZ_4K, .types = fail ? fail_types : NULL },
	};
	struct fixture_master *master = kzalloc(sizeof(*master), GFP_KERNEL);
	struct mtd_info *after;
	int ret;

	if (!master)
		return ERR_PTR(-ENOMEM);
	mutex_init(&master->lock);
	master->value = value;
	master->mtd.name = id ? "concat-master-b" : "concat-master-a";
	master->mtd.owner = THIS_MODULE;
	master->mtd.parent_dev = parent;
	master->mtd.type = MTD_ROM;
	master->mtd.flags = MTD_CAP_ROM | MTD_NO_ERASE;
	master->mtd.size = SZ_16K;
	master->mtd.writesize = incompatible ? 2 : 1;
	master->mtd._read = fixture_read;
	master->mtd.oobsize = 16;
	master->mtd.oobavail = 4;
	if (layout_mask & BIT(id))
		mtd_set_ooblayout(&master->mtd, &fixture_ooblayout);
	if (!unmanaged)
		mtd_set_release(&master->mtd, fixture_release);
	ret = mtd_device_register(&master->mtd, parts, ARRAY_SIZE(parts));
	if (ret) {
		master->disconnected = true;
		fixture_put(&master->mtd);
		return ERR_PTR(ret);
	}
	after = get_mtd_device_nm(parts[1].name);
	if (IS_ERR(after))
		ret = PTR_ERR(after);
	else {
		ret = after->part.offset == SZ_4K ? 0 : -EINVAL;
		put_mtd_device(after);
	}
	if (ret) {
		mtd_device_unregister(&master->mtd);
		master->disconnected = true;
		fixture_put(&master->mtd);
		return ERR_PTR(ret);
	}
	return master;
}

static void fixture_remove(struct fixture_master *master)
{
	WARN_ON(mtd_device_unregister(&master->mtd));
	scoped_guard(mutex, &master->lock)
		master->disconnected = true;
	fixture_put(&master->mtd);
}

static int expect_read(struct mtd_info *mtd, loff_t off, int result, u8 value)
{
	size_t retlen = 0;
	u8 byte = 0;
	int ret = mtd_read(mtd, off, 1, &retlen, &byte);

	return ret != result || (!ret && (retlen != 1 || byte != value)) ? -EINVAL : 0;
}

static struct mtd_info *get_concat(void)
{
	return get_mtd_device_nm("concat-a-concat-b-concat");
}

static int expect_layout(struct mtd_info *mtd, int result, u8 value)
{
	struct mtd_oob_region region;
	int ret;

	ret = mtd_ooblayout_free(mtd, 0, &region);
	if (ret != result || (!ret &&
	    (region.offset != (value & 3) || region.length != 4)))
		return -EINVAL;
	ret = mtd_ooblayout_ecc(mtd, 0, &region);
	if (ret != result || (!ret &&
	    (region.offset != (value & 3) + 4 || region.length != 8)))
		return -EINVAL;
	if (!result && (mtd_ooblayout_free(mtd, 1, &region) != -ERANGE ||
			mtd_ooblayout_ecc(mtd, 1, &region) != -ERANGE))
		return -EINVAL;
	return 0;
}

static int partition_layout(void)
{
	struct fixture_master *a, *b;
	struct mtd_info *concat, *nested = NULL, *part;
	unsigned int before = atomic_read(&releases);
	int calls, ret, expected = layout_mask & BIT(0) ? 0 : -ENOTSUPP;

	b = fixture_register(1, 0xb2, false, false);
	if (IS_ERR(b))
		return PTR_ERR(b);
	a = fixture_register(0, 0xa1, false, false);
	if (IS_ERR(a)) {
		fixture_remove(b);
		return PTR_ERR(a);
	}
	concat = get_concat();
	if (IS_ERR(concat)) {
		ret = PTR_ERR(concat);
		goto out_masters;
	}
	/* Partition layout pointers stay NULL; public helpers use the master. */
	part = get_mtd_device_nm("concat-after-a");
	if (IS_ERR(part)) {
		ret = PTR_ERR(part);
		goto out_concat;
	}
	ret = part->ooblayout ? -EINVAL : expect_layout(part, expected, 0xa1);
	put_mtd_device(part);
	ret |= expect_layout(&a->mtd, expected, 0xa1);
	ret |= expect_layout(concat, expected, 0xa1);
	if (ret)
		goto out_concat;
	ret = mtd_add_partition(concat, "concat-oob-nested", 0, SZ_4K);
	if (ret)
		goto out_concat;
	nested = get_mtd_device_nm("concat-oob-nested");
	if (IS_ERR(nested)) {
		ret = PTR_ERR(nested);
		nested = NULL;
		goto out_concat;
	}
	ret = nested->ooblayout ? -EINVAL : expect_layout(nested, expected, 0xa1);
	fixture_remove(a);
	a = NULL;
	fixture_remove(b);
	b = NULL;
	calls = atomic_read(&layout_calls);
	if (!expected)
		expected = -ENODEV;
	ret |= expect_layout(concat, expected, 0) | expect_layout(nested, expected, 0);
	if (atomic_read(&layout_calls) != calls)
		ret = -EINVAL;
out_concat:
	if (nested)
		put_mtd_device(nested);
	put_mtd_device(concat);
out_masters:
	if (a)
		fixture_remove(a);
	if (b)
		fixture_remove(b);
	if (atomic_read(&releases) != before + 2)
		ret = -EINVAL;
	pr_info("concat-oob: mask %u, %s masters: %s\n", layout_mask,
		unmanaged ? "unmanaged" : "managed", ret ? "FAIL" : "PASS");
	return ret;
}

static int retained_rebind(void)
{
	struct fixture_master *a, *b, *replacement;
	struct mtd_info *old, *new, *lookup;
	struct nvmem_device *nvmem;
	unsigned int before = atomic_read(&releases);
	int ret;
	u8 byte;

	b = fixture_register(1, 0xb2, false, false);
	if (IS_ERR(b))
		return PTR_ERR(b);
	a = fixture_register(0, 0xa1, false, false);
	if (IS_ERR(a)) {
		fixture_remove(b);
		return PTR_ERR(a);
	}
	old = get_concat();
	if (IS_ERR(old)) {
		fixture_remove(a);
		fixture_remove(b);
		return PTR_ERR(old);
	}
	ret = expect_read(old, 0, 0, 0xa1) | expect_read(old, SZ_4K, 0, 0xb2);
	nvmem = nvmem_device_get(parent, dev_name(old->dev));
	if (IS_ERR(nvmem)) {
		ret = PTR_ERR(nvmem);
		goto out_old;
	}
	/* Exercise virtual-master partition locking during physical removal. */
	ret |= mtd_add_partition(old, "concat-nested", 0, SZ_4K);
	fixture_remove(a);
	a = NULL;
	lookup = get_concat();
	if (!IS_ERR(lookup)) {
		ret = -EINVAL;
		put_mtd_device(lookup);
	}
	ret |= expect_read(old, 0, -ENODEV, 0);
	if (nvmem_device_read(nvmem, 0, 1, &byte) != -ENODEV)
		ret = -EINVAL;
	replacement = fixture_register(0, 0xa3, false, false);
	if (IS_ERR(replacement)) {
		ret = PTR_ERR(replacement);
		goto out_nvmem;
	}
	new = get_concat();
	if (IS_ERR(new)) {
		ret = PTR_ERR(new);
		fixture_remove(replacement);
		goto out_nvmem;
	}
	ret |= expect_read(new, 0, 0, 0xa3) | expect_read(old, 0, -ENODEV, 0);
	fixture_remove(b);
	b = NULL;
	fixture_remove(replacement);
	ret |= expect_read(old, SZ_4K, -ENODEV, 0);
	put_mtd_device(new);
out_nvmem:
	nvmem_device_put(nvmem);
out_old:
	put_mtd_device(old);
	if (a)
		fixture_remove(a);
	if (b)
		fixture_remove(b);
	if (atomic_read(&releases) != before + 3)
		ret = -EINVAL;
	pr_info("concat-lifetime: retained handles, NVMEM and rebind: %s\n",
		ret ? "FAIL" : "PASS");
	return ret;
}

static int failed_registration(void)
{
	struct fixture_master *a, *b;
	struct mtd_info *mtd;
	unsigned int before = atomic_read(&releases);
	int ret = 0;

	b = fixture_register(1, 0xb2, false, false);
	if (IS_ERR(b))
		return PTR_ERR(b);
	a = fixture_register(0, 0xa1, false, true);
	if (!IS_ERR(a)) {
		fixture_remove(a);
		ret = -EINVAL;
	} else if (PTR_ERR(a) != -EIO) {
		ret = PTR_ERR(a);
	}
	a = fixture_register(0, 0xa1, true, false);
	if (!IS_ERR(a)) {
		fixture_remove(a);
		ret = -EINVAL;
	} else if (PTR_ERR(a) != -ENXIO) {
		ret = PTR_ERR(a);
	}
	a = fixture_register(0, 0xa3, false, false);
	if (IS_ERR(a)) {
		ret = PTR_ERR(a);
	} else {
		mtd = get_concat();
		if (IS_ERR(mtd))
			ret = PTR_ERR(mtd);
		else {
			ret |= expect_read(mtd, 0, 0, 0xa3);
			put_mtd_device(mtd);
		}
		fixture_remove(a);
	}
	fixture_remove(b);
	if (atomic_read(&releases) != before + 4)
		ret = -EINVAL;
	pr_info("concat-lifetime: partial registration and join rollback: %s\n",
		ret ? "FAIL" : "PASS");
	return ret;
}

struct remove_job {
	struct fixture_master *master;
	struct completion *start;
	struct completion done;
};

static int remove_thread(void *arg)
{
	struct remove_job *job = arg;

	wait_for_completion(job->start);
	fixture_remove(job->master);
	complete(&job->done);
	return 0;
}

static int concurrent_remove(void)
{
	DECLARE_COMPLETION_ONSTACK(start);
	struct remove_job jobs[2];
	struct task_struct *tasks[2];
	struct mtd_info *mtd;
	unsigned int before = atomic_read(&releases);
	int i, ret = 0;

	for (i = 0; i < 2; i++) {
		jobs[i].master = fixture_register(i, 0xa0 + i, false, false);
		if (IS_ERR(jobs[i].master)) {
			if (i)
				fixture_remove(jobs[0].master);
			return PTR_ERR(jobs[i].master);
		}
		jobs[i].start = &start;
		init_completion(&jobs[i].done);
	}
	mtd = get_concat();
	if (IS_ERR(mtd)) {
		fixture_remove(jobs[0].master);
		fixture_remove(jobs[1].master);
		return PTR_ERR(mtd);
	}
	for (i = 0; i < 2; i++) {
		tasks[i] = kthread_create(remove_thread, &jobs[i], "concat-remove-%d", i);
		if (!IS_ERR(tasks[i])) {
			get_task_struct(tasks[i]);
			wake_up_process(tasks[i]);
		}
	}
	complete_all(&start);
	for (i = 0; i < 2; i++) {
		if (IS_ERR(tasks[i])) {
			fixture_remove(jobs[i].master);
			ret = PTR_ERR(tasks[i]);
		} else {
			wait_for_completion(&jobs[i].done);
			kthread_stop(tasks[i]);
			put_task_struct(tasks[i]);
		}
	}
	ret |= expect_read(mtd, 0, -ENODEV, 0) | expect_read(mtd, SZ_4K, -ENODEV, 0);
	put_mtd_device(mtd);
	if (atomic_read(&releases) != before + 2)
		ret = -EINVAL;
	return ret;
}

static int run_tests(const char *val, const struct kernel_param *kp)
{
	struct device_node *fixture_root;
	int ret, i;
	void *blob;

	if (of_root && of_root->child)
		return -EOPNOTSUPP;
	/* The fixture stays in RAM until this disposable QEMU guest exits. */
	blob = kmemdup(concat_fixture_dtb, concat_fixture_dtb_len, GFP_KERNEL);
	if (!blob)
		return -ENOMEM;
	if (!of_fdt_unflatten_tree(blob, of_root, &fixture_root)) {
		kfree(blob);
		return -ENOMEM;
	}
	if (!of_root)
		of_root = fixture_root;
	nodes[0] = of_get_child_by_name(fixture_root, "part-a");
	nodes[1] = of_get_child_by_name(fixture_root, "part-b");
	if (!nodes[0] || !nodes[1])
		return -EINVAL;
	parent = root_device_register("concat-lifetime");
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	register_mtd_parser(&fail_parser);
	ret = 0;
	for (layout_mask = 0; !ret && layout_mask < 4; layout_mask++)
		ret = partition_layout();
	layout_mask = 0;
	if (!ret)
		ret = retained_rebind();
	if (!ret)
		ret = failed_registration();
	for (i = 0; !ret && i < 25; i++)
		ret = concurrent_remove();
	if (!ret) {
		unmanaged = true;
		for (layout_mask = 0; !ret && layout_mask < 4; layout_mask++)
			ret = partition_layout();
		layout_mask = 0;
	}
	if (!ret)
		ret = retained_rebind();
	if (!ret)
		ret = failed_registration();
	for (i = 0; !ret && i < 25; i++)
		ret = concurrent_remove();
	deregister_mtd_parser(&fail_parser);
	root_device_unregister(parent);
	of_node_put(nodes[0]);
	of_node_put(nodes[1]);
	pr_info("concat-lifetime: %s (%u releases, %d concurrent cycles)\n",
		ret ? "FAIL" : "PASS", atomic_read(&releases), i);
	return ret;
}

static const struct kernel_param_ops run_ops = { .set = run_tests };
module_param_cb(run, &run_ops, NULL, 0200);
static int __init fixture_init(void) { return 0; }
static void __exit fixture_exit(void) { }
module_init(fixture_init);
module_exit(fixture_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Retained virtual-concat ownership and failure-path checks");
