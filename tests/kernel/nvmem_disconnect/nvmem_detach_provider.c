// SPDX-License-Identifier: GPL-2.0-or-later
/* A disposable provider. No physical NVMEM or flash is accessed. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/slab.h>

#include "test.h"

struct test_context {
	u8 value;
};

static struct test_context *context;
static struct device *parent;
static struct nvmem_device *provider;

static int test_read(void *priv, unsigned int off, void *buf, size_t len)
{
	struct test_context *ctx = priv;
	int ret = nvmem_detach_test_gate(1);

	if (ret)
		return ret;
	memset(buf, ctx->value, len);
	return 0;
}

static int test_write(void *priv, unsigned int off, void *buf, size_t len)
{
	struct test_context *ctx = priv;
	int ret = nvmem_detach_test_gate(2);

	if (ret)
		return ret;
	if (len)
		ctx->value = *(u8 *)buf;
	return 0;
}

static int test_postprocess(void *priv, const char *id, int index,
			    unsigned int off, void *buf, size_t len)
{
	struct test_context *ctx = priv;
	int ret = nvmem_detach_test_gate(3);

	if (ret)
		return ret;
	memset(buf, ctx->value, len);
	return 0;
}

static int __init provider_init(void)
{
	struct nvmem_cell_info cell = {
		.name = "token", .offset = 0, .bytes = 1,
		.read_post_process = test_postprocess,
	};
	struct nvmem_config config = {
		.name = "nvmem-detach-fixture", .id = NVMEM_DEVID_NONE,
		.owner = THIS_MODULE, .size = 8, .word_size = 1, .stride = 1,
		.ignore_wp = true, .reg_read = test_read, .reg_write = test_write,
		.cells = &cell, .ncells = 1,
	};
	int ret;

	parent = root_device_register("nvmem-detach-provider");
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context) {
		ret = -ENOMEM;
		goto unregister_parent;
	}
	context->value = 0xa5;
	config.dev = parent;
	config.priv = context;
	cell.priv = context;
	provider = nvmem_register(&config);
	if (IS_ERR(provider)) {
		ret = PTR_ERR(provider);
		kfree(context);
		goto unregister_parent;
	}
	return 0;

unregister_parent:
	root_device_unregister(parent);
	return ret;
}

static void __exit provider_exit(void)
{
	pr_info("NVMEM-DETACH: unregister entered\n");
	nvmem_unregister(provider);
	kfree(context);
	root_device_unregister(parent);
	pr_info("NVMEM-DETACH: provider context freed\n");
}

module_init(provider_init);
module_exit(provider_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Disposable NVMEM provider for disconnect testing");
