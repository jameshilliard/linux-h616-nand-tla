// SPDX-License-Identifier: GPL-2.0-or-later
/* Synthetic devices only: this test never accesses flash hardware. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/sizes.h>
#include <linux/slab.h>

struct test_mtd {
	struct mtd_info mtd;
	struct mutex lock;
	bool disconnected;
};

static struct device *parent;
static unsigned int releases;
static bool expect_collision;
module_param(expect_collision, bool, 0444);

static int test_read(struct mtd_info *mtd, loff_t from, size_t len,
		     size_t *retlen, u_char *buf)
{
	struct test_mtd *test = container_of(mtd, struct test_mtd, mtd);
	int ret = 0;

	mutex_lock(&test->lock);
	if (test->disconnected) {
		ret = -ENODEV;
	} else {
		memset(buf, 0xff, len);
		*retlen = len;
	}
	mutex_unlock(&test->lock);
	return ret;
}

static void test_release(struct mtd_info *mtd)
{
	struct test_mtd *test = container_of(mtd, struct test_mtd, mtd);

	releases++;
	kfree(test);
}

static struct test_mtd *test_register(bool partitions)
{
	static const struct mtd_partition part = {
		.name = "retained-partition",
		.size = SZ_32K,
	};
	struct test_mtd *test;
	int ret;

	test = kzalloc(sizeof(*test), GFP_KERNEL);
	if (!test)
		return ERR_PTR(-ENOMEM);
	mutex_init(&test->lock);
	test->mtd.name = "retained-lifetime-test";
	test->mtd.owner = THIS_MODULE;
	test->mtd.dev.parent = parent;
	test->mtd.type = MTD_ROM;
	test->mtd.flags = MTD_CAP_ROM | MTD_NO_ERASE;
	test->mtd.size = SZ_64K;
	test->mtd.writesize = 1;
	test->mtd._read = test_read;
	mtd_set_release(&test->mtd, test_release);
	ret = mtd_device_register(&test->mtd, partitions ? &part : NULL,
				 partitions ? 1 : 0);
	if (ret) {
		mtd_put_master(&test->mtd);
		return ERR_PTR(ret);
	}
	return test;
}

static void test_remove(struct test_mtd *test)
{
	WARN_ON(mtd_device_unregister(&test->mtd));
	mutex_lock(&test->lock);
	test->disconnected = true;
	mutex_unlock(&test->lock);
	mtd_put_master(&test->mtd);
}

/* Keep an ordinary MTD reference through unregister and a rejected read. */
static int test_handle(void)
{
	struct test_mtd *test = test_register(false);
	struct mtd_info *handle, *lookup;
	unsigned int before = releases;
	size_t retlen = 0;
	int index, ret = 0;
	u8 byte;

	if (IS_ERR(test))
		return PTR_ERR(test);
	handle = get_mtd_device(&test->mtd, -1);
	if (IS_ERR(handle)) {
		test_remove(test);
		return PTR_ERR(handle);
	}
	index = handle->index;
	test_remove(test);
	if (releases != before ||
	    mtd_read(handle, 0, 1, &retlen, &byte) != -ENODEV)
		ret = -EINVAL;
	lookup = get_mtd_device(NULL, index);
	if (!IS_ERR(lookup)) {
		ret = -EINVAL;
		put_mtd_device(lookup);
	}
	put_mtd_device(handle);
	if (releases != before + 1)
		ret = -EINVAL;
	return ret;
}

/* A bare device reference must retain the backing allocation too. */
static int test_device(void)
{
	struct test_mtd *test = test_register(false);
	unsigned int before = releases;
	struct device *dev;
	int ret = 0;

	if (IS_ERR(test))
		return PTR_ERR(test);
	dev = get_device(&test->mtd.dev);
	test_remove(test);
	if (releases != before)
		ret = -EINVAL;
	put_device(dev);
	if (releases != before + 1)
		ret = -EINVAL;
	return ret;
}

/* Retain the old NVMEM provider while registering a replacement MTD. */
static int test_nvmem(void)
{
	struct test_mtd *old = test_register(false), *replacement;
	struct nvmem_device *nvmem;
	unsigned int before = releases;
	int index, ret = 0;
	u8 byte;

	if (IS_ERR(old))
		return PTR_ERR(old);
	index = old->mtd.index;
	nvmem = nvmem_device_get(parent, dev_name(&old->mtd.dev));
	if (IS_ERR(nvmem)) {
		test_remove(old);
		return PTR_ERR(nvmem);
	}
	test_remove(old);
	if (releases != before ||
	    nvmem_device_read(nvmem, 0, 1, &byte) != -ENODEV)
		ret = -EINVAL;
	replacement = test_register(false);
	if (IS_ERR(replacement)) {
		pr_info("retained-lifetime: replacement failed with %ld\n",
			PTR_ERR(replacement));
		if (!expect_collision || PTR_ERR(replacement) != -EEXIST)
			ret = -EINVAL;
	} else {
		pr_info("retained-lifetime: replacement index %d, retained index %d\n",
			replacement->mtd.index, index);
		if (expect_collision || replacement->mtd.index == index)
			ret = -EINVAL;
		test_remove(replacement);
	}
	nvmem_device_put(nvmem);
	if (releases != before + 2)
		ret = -EINVAL;
	/* With both old devices released, their number can be reused. */
	replacement = test_register(false);
	if (IS_ERR(replacement))
		return PTR_ERR(replacement);
	if (replacement->mtd.index != index)
		ret = -EINVAL;
	test_remove(replacement);
	return ret;
}

static int test_partitions(void)
{
	struct test_mtd *test = test_register(true);
	struct mtd_info *part, *nested;
	struct nvmem_device *nvmem;
	struct device *dev;
	unsigned int before = releases;
	int ret;
	u8 byte;

	if (IS_ERR(test))
		return PTR_ERR(test);
	part = get_mtd_device_nm("retained-partition");
	if (IS_ERR(part)) {
		test_remove(test);
		return PTR_ERR(part);
	}
	ret = mtd_add_partition(part, "retained-nested", 0, SZ_4K);
	if (ret)
		goto out_part;
	nested = get_mtd_device_nm("retained-nested");
	if (IS_ERR(nested)) {
		ret = PTR_ERR(nested);
		goto out_part;
	}
	nvmem = nvmem_device_get(parent, dev_name(&nested->dev));
	if (IS_ERR(nvmem)) {
		ret = PTR_ERR(nvmem);
		put_mtd_device(nested);
		goto out_part;
	}
	dev = get_device(&nested->dev);
	put_mtd_device(nested);
	put_mtd_device(part);
	test_remove(test);
	ret = 0;
	if (releases != before ||
	    nvmem_device_read(nvmem, 0, 1, &byte) != -ENODEV)
		ret = -EINVAL;
	nvmem_device_put(nvmem);
	if (releases != before)
		ret = -EINVAL;
	put_device(dev);
	if (releases != before + 1)
		ret = -EINVAL;
	return ret;

out_part:
	put_mtd_device(part);
	test_remove(test);
	return ret;
}

static int retained_test_run(const char *val, const struct kernel_param *kp)
{
	int ret;

	parent = root_device_register("retained-lifetime-test");
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	ret = test_handle();
	if (!ret)
		ret = test_device();
	if (!ret)
		ret = test_nvmem();
	if (!ret)
		ret = test_partitions();
	root_device_unregister(parent);
	pr_info("retained-lifetime: %s (%u releases)\n", ret ? "FAIL" : "PASS",
		releases);
	return ret;
}

static const struct kernel_param_ops run_ops = {
	.set = retained_test_run,
};
module_param_cb(run, &run_ops, NULL, 0200);

static int __init retained_test_init(void)
{
	return 0;
}

static void __exit retained_test_exit(void)
{
}

module_init(retained_test_init);
module_exit(retained_test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synthetic retained MTD handle and NVMEM rebind checks");
