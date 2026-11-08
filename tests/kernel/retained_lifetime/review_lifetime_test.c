// SPDX-License-Identifier: GPL-2.0-or-later
/* Review regressions using synthetic devices; never accesses flash. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/nvmem-provider.h>
#include <linux/sizes.h>
#include <linux/slab.h>

static struct device *parent;
static unsigned int releases, callback_errors, context_puts;

static int test_read(struct mtd_info *mtd, loff_t off, size_t len,
		     size_t *retlen, u_char *buf)
{
	memset(buf, 0xff, len);
	*retlen = len;
	return 0;
}

static void backing_release(struct mtd_info *mtd)
{
	releases++;
	kfree(mtd);
}

static struct mtd_info *new_master(void)
{
	struct mtd_info *mtd = kzalloc(sizeof(*mtd), GFP_KERNEL);

	if (!mtd)
		return NULL;
	mtd->name = "review-master";
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = parent;
	mtd->type = MTD_ROM;
	mtd->flags = MTD_CAP_ROM | MTD_NO_ERASE;
	mtd->size = SZ_64K;
	mtd->writesize = 1;
	mtd->_read = test_read;
	mtd_set_release(mtd, backing_release);
	return mtd;
}

/* A child get/put must not change meaning when its master is published. */
static int changing_master_type(void)
{
	const struct mtd_partition part = {
		.name = "review-type-change", .size = SZ_32K,
	};
	struct mtd_info *master = new_master(), *child;
	int ret;

	if (!master)
		return -ENOMEM;
	ret = mtd_device_register(master, &part, 1);
	if (ret)
		goto put_master;
	child = get_mtd_device_nm(part.name);
	if (IS_ERR(child)) {
		ret = PTR_ERR(child);
		goto unregister;
	}
	if (master->dev.type) {
		ret = -EINVAL;
		goto put_child;
	}
	ret = mtd_del_partition(master, child->index);
	if (ret)
		goto put_child;
	ret = mtd_device_register(master, NULL, 0);
	if (ret)
		goto put_child;
	put_mtd_device(child);
	ret = master->registered && device_is_registered(&master->dev) &&
	      kref_read(&master->refcnt) == 1 ? 0 : -EINVAL;
	pr_info("review-lifetime: stable master reference %s\n", ret ? "FAIL" : "PASS");
	goto unregister;
put_child:
	put_mtd_device(child);
unregister:
	mtd_device_unregister(master);
put_master:
	mtd_put_master(master);
	return ret;
}

static int post_unregister_partition(void)
{
	struct mtd_info *master = new_master(), *handle;
	int ret;

	if (!master)
		return -ENOMEM;
	ret = mtd_device_register(master, NULL, 0);
	if (ret)
		goto put_master;
	handle = get_mtd_device(master, -1);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mtd_device_unregister(master);
		goto put_master;
	}
	ret = mtd_device_unregister(master);
	if (!ret) {
		ret = mtd_add_partition(handle, "review-after-unregister", 0, SZ_32K);
		if (!ret)
			mtd_device_unregister(master);
		ret = ret == -ENODEV ? 0 : -EINVAL;
	}
	pr_info("review-lifetime: reject late partition %s\n", ret ? "FAIL" : "PASS");
	put_mtd_device(handle);
put_master:
	mtd_put_master(master);
	return ret;
}

static void context_get(void *priv)
{
	if (module_refcount(THIS_MODULE) != *(int *)priv + 1)
		callback_errors++;
}

static void context_put(void *priv)
{
	/* The caller's reference must still exist inside the callback. */
	if (module_refcount(THIS_MODULE) != *(int *)priv + 1)
		callback_errors++;
	context_puts++;
}

static int context_read(void *priv, unsigned int off, void *buf, size_t len)
{
	memset(buf, 0xff, len);
	return 0;
}

static int callback_owner(void)
{
	int refs = module_refcount(THIS_MODULE);
	struct nvmem_config config = {
		.dev = parent,
		.name = "review-context",
		.id = NVMEM_DEVID_AUTO,
		.owner = THIS_MODULE,
		.size = 32,
		.ignore_wp = true,
		.reg_read = context_read,
		.priv = &refs,
		.priv_get = context_get,
		.priv_put = context_put,
		.priv_owner = THIS_MODULE,
	};
	struct nvmem_device *nvmem = nvmem_register(&config);
	int ret;

	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);
	nvmem_unregister(nvmem);
	ret = !callback_errors && context_puts == 1 &&
	      module_refcount(THIS_MODULE) == refs ? 0 : -EINVAL;
	pr_info("review-lifetime: callback owner reference %s\n", ret ? "FAIL" : "PASS");
	return ret;
}

static int run_tests(const char *value, const struct kernel_param *kp)
{
	int ret = changing_master_type();

	if (!ret)
		ret = post_unregister_partition();
	if (!ret)
		ret = callback_owner();
	if (!ret && releases != 2)
		ret = -EINVAL;
	pr_info("review-lifetime: %s (%u backing releases)\n",
		ret ? "FAIL" : "PASS", releases);
	return ret;
}

static const struct kernel_param_ops run_ops = { .set = run_tests };
module_param_cb(run, &run_ops, NULL, 0200);

static int __init test_init(void)
{
	if (IS_ENABLED(CONFIG_MTD_PARTITIONED_MASTER))
		return -EINVAL;
	parent = root_device_register("review-lifetime-test");
	return PTR_ERR_OR_ZERO(parent);
}

static void __exit test_exit(void)
{
	root_device_unregister(parent);
}

module_init(test_init);
module_exit(test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synthetic MTD and NVMEM review regression tests");
