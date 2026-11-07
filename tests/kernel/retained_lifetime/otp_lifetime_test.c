// SPDX-License-Identifier: GPL-2.0-or-later
/* Synthetic MTD/NVMEM objects only; never accesses physical flash. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/slab.h>

struct test_mtd {
	struct mtd_info mtd;
	bool disconnected;
	int factory_error;
};

static struct device *parent;
static unsigned int releases, context_gets, context_puts;

static int test_read(struct mtd_info *mtd, loff_t off, size_t len,
		     size_t *retlen, u_char *buf)
{
	struct test_mtd *test = container_of(mtd, struct test_mtd, mtd);

	if (test->disconnected)
		return -ENODEV;
	memset(buf, 0xa5, len);
	*retlen = len;
	return 0;
}

static int test_otp_info(struct mtd_info *mtd, size_t len, size_t *retlen,
			 struct otp_info *buf)
{
	if (len < sizeof(*buf))
		return -EINVAL;
	buf->start = 0;
	buf->length = 32;
	buf->locked = 1;
	*retlen = sizeof(*buf);
	return 0;
}

static int test_factory_info(struct mtd_info *mtd, size_t len,
			     size_t *retlen, struct otp_info *buf)
{
	struct test_mtd *test = container_of(mtd, struct test_mtd, mtd);

	if (test->factory_error)
		return test->factory_error;
	return test_otp_info(mtd, len, retlen, buf);
}

static void test_release(struct mtd_info *mtd)
{
	releases++;
	kfree(container_of(mtd, struct test_mtd, mtd));
}

static struct test_mtd *test_alloc(void)
{
	struct test_mtd *test = kzalloc(sizeof(*test), GFP_KERNEL);

	if (!test)
		return NULL;
	test->mtd.name = "otp-lifetime-test";
	test->mtd.owner = THIS_MODULE;
	test->mtd.dev.parent = parent;
	test->mtd.type = MTD_ROM;
	test->mtd.flags = MTD_CAP_ROM | MTD_NO_ERASE;
	test->mtd.size = 4096;
	test->mtd.writesize = 1;
	test->mtd._read = test_read;
	test->mtd._get_user_prot_info = test_otp_info;
	test->mtd._get_fact_prot_info = test_factory_info;
	test->mtd._read_user_prot_reg = test_read;
	test->mtd._read_fact_prot_reg = test_read;
	mtd_set_release(&test->mtd, test_release);
	return test;
}

static void test_remove(struct test_mtd *test)
{
	WARN_ON(mtd_device_unregister(&test->mtd));
	test->disconnected = true;
	mtd_put_master(&test->mtd);
}

/* Hold both provider kinds through unbind, then release them in either order. */
static int test_consumers(bool factory_first)
{
	struct test_mtd *test = test_alloc(), *replacement;
	struct nvmem_device *user, *factory, *first, *last;
	unsigned int before = releases;
	int ret, refs;
	u8 byte;

	if (!test)
		return -ENOMEM;
	ret = mtd_device_register(&test->mtd, NULL, 0);
	if (ret) {
		mtd_put_master(&test->mtd);
		return ret;
	}
	refs = module_refcount(THIS_MODULE);
	user = nvmem_device_get(parent, nvmem_dev_name(test->mtd.otp_user_nvmem));
	if (IS_ERR(user)) {
		ret = PTR_ERR(user);
		goto remove;
	}
	factory = nvmem_device_get(parent,
				   nvmem_dev_name(test->mtd.otp_factory_nvmem));
	if (IS_ERR(factory)) {
		ret = PTR_ERR(factory);
		nvmem_device_put(user);
		goto remove;
	}
	ret = 0;
	if (module_refcount(THIS_MODULE) != refs + 2 ||
	    nvmem_device_read(user, 0, 1, &byte) != 1 || byte != 0xa5 ||
	    nvmem_device_read(factory, 0, 1, &byte) != 1 || byte != 0xa5)
		ret = -EINVAL;
	test_remove(test);
	if (releases != before) {
		pr_err("otp-lifetime: backing freed with OTP consumers held\n");
		ret = -EINVAL;
	} else if (nvmem_device_read(user, 0, 1, &byte) != -ENODEV ||
		   nvmem_device_read(factory, 0, 1, &byte) != -ENODEV) {
		ret = -EINVAL;
	}
	replacement = test_alloc();
	if (!replacement) {
		ret = -ENOMEM;
	} else {
		int err = mtd_device_register(&replacement->mtd, NULL, 0);

		if (err) {
			mtd_put_master(&replacement->mtd);
			ret = err;
		} else {
			test_remove(replacement);
		}
		before++;
	}
	first = factory_first ? factory : user;
	last = factory_first ? user : factory;
	nvmem_device_put(first);
	if (releases != before)
		ret = -EINVAL;
	nvmem_device_put(last);
	if (releases != before + 1 || module_refcount(THIS_MODULE) != refs)
		ret = -EINVAL;
	return ret;

remove:
	test_remove(test);
	return ret;
}

/* User OTP has registered before factory discovery or MTD registration fails. */
static int test_registration_error(int factory_error, bool invalid_mtd)
{
	struct test_mtd *test = test_alloc();
	unsigned int before = releases;
	int ret, expected;

	if (!test)
		return -ENOMEM;
	test->factory_error = factory_error;
	if (invalid_mtd)
		test->mtd.flags |= MTD_SLC_ON_MLC_EMULATION;
	expected = invalid_mtd ? -EINVAL : factory_error;
	if (expected == -EOPNOTSUPP)
		expected = 0;
	ret = mtd_device_register(&test->mtd, NULL, 0);
	if (!ret)
		WARN_ON(mtd_device_unregister(&test->mtd));
	if (test->mtd.otp_user_nvmem || test->mtd.otp_factory_nvmem)
		ret = -EINVAL;
	test->disconnected = true;
	mtd_put_master(&test->mtd);
	return ret == expected && releases == before + 1 ? 0 : -EINVAL;
}

static int test_device_reference(void)
{
	struct test_mtd *test = test_alloc();
	unsigned int before = releases;
	struct device *dev;
	int ret;

	if (!test)
		return -ENOMEM;
	ret = mtd_device_register(&test->mtd, NULL, 0);
	if (ret) {
		mtd_put_master(&test->mtd);
		return ret;
	}
	dev = device_find_child(parent, nvmem_dev_name(test->mtd.otp_user_nvmem),
				device_match_name);
	if (!dev) {
		test_remove(test);
		return -ENOENT;
	}
	test_remove(test);
	ret = releases == before ? 0 : -EINVAL;
	put_device(dev);
	if (releases != before + 1)
		ret = -EINVAL;
	return ret;
}

static struct nvmem_device *probe_consumer;

static int probe_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	struct nvmem_device *nvmem = data;

	if (action == NVMEM_ADD && !strncmp(nvmem_dev_name(nvmem), "user-otp", 8))
		probe_consumer = nvmem_device_get(parent, nvmem_dev_name(nvmem));
	return NOTIFY_OK;
}

static int test_failed_probe_consumer(void)
{
	struct notifier_block nb = { .notifier_call = probe_notify };
	struct test_mtd *test = test_alloc();
	unsigned int before = releases;
	int ret, err;
	u8 byte;

	if (!test)
		return -ENOMEM;
	test->factory_error = -EIO;
	probe_consumer = NULL;
	ret = nvmem_register_notifier(&nb);
	if (ret) {
		mtd_put_master(&test->mtd);
		return ret;
	}
	err = mtd_device_register(&test->mtd, NULL, 0);
	nvmem_unregister_notifier(&nb);
	if (!err)
		mtd_device_unregister(&test->mtd);
	test->disconnected = true;
	mtd_put_master(&test->mtd);
	if (IS_ERR_OR_NULL(probe_consumer))
		return -EINVAL;
	ret = err == -EIO && releases == before ? 0 : -EINVAL;
	if (!ret && nvmem_device_read(probe_consumer, 0, 1, &byte) != -ENODEV)
		ret = -EINVAL;
	nvmem_device_put(probe_consumer);
	probe_consumer = NULL;
	if (releases != before + 1)
		ret = -EINVAL;
	return ret;
}

static void context_get(void *priv)
{
	context_gets++;
}

static void context_put(void *priv)
{
	context_puts++;
}

static int context_read(void *priv, unsigned int off, void *buf, size_t len)
{
	memset(buf, 0xa5, len);
	return 0;
}

/* A bad configuration is rejected before get; later errors balance get/put. */
static int test_provider_errors(void)
{
	const struct nvmem_keepout invalid = { .start = 2, .end = 1 };
	struct nvmem_config config = {
		.dev = parent,
		.name = "otp-context-test",
		.id = NVMEM_DEVID_AUTO,
		.owner = THIS_MODULE,
		.size = 32,
		.ignore_wp = true,
		.reg_read = context_read,
		.priv_get = context_get,
		.keepout = &invalid,
		.nkeepout = 1,
	};
	struct nvmem_device *nvmem;
	unsigned int gets = context_gets, puts = context_puts;

	nvmem = nvmem_register(&config);
	if (!IS_ERR(nvmem)) {
		nvmem_unregister(nvmem);
		return -EINVAL;
	}
	if (PTR_ERR(nvmem) != -EINVAL || context_gets != gets || context_puts != puts)
		return -EINVAL;
	config.priv_put = context_put;
	nvmem = nvmem_register(&config);
	if (!IS_ERR(nvmem)) {
		nvmem_unregister(nvmem);
		return -EINVAL;
	}
	return PTR_ERR(nvmem) == -EINVAL && context_gets == gets + 1 &&
	       context_puts == puts + 1 ? 0 : -EINVAL;
}

static int test_run(const char *val, const struct kernel_param *kp)
{
	int ret;

	parent = root_device_register("otp-lifetime-test");
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	ret = test_consumers(false);
	if (!ret)
		ret = test_consumers(true);
	if (!ret)
		ret = test_registration_error(-EIO, false);
	if (!ret)
		ret = test_registration_error(-EOPNOTSUPP, false);
	if (!ret)
		ret = test_registration_error(0, true);
	if (!ret)
		ret = test_provider_errors();
	if (!ret)
		ret = test_device_reference();
	if (!ret)
		ret = test_failed_probe_consumer();
	root_device_unregister(parent);
	pr_info("otp-lifetime: %s (%u backing releases)\n",
		ret ? "FAIL" : "PASS", releases);
	return ret;
}

static const struct kernel_param_ops run_ops = { .set = test_run };
module_param_cb(run, &run_ops, NULL, 0200);

static int __init test_init(void)
{
	return 0;
}

static void __exit test_exit(void)
{
}

module_init(test_init);
module_exit(test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synthetic OTP NVMEM backing lifetime and unwind checks");
