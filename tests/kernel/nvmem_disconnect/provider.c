// SPDX-License-Identifier: GPL-2.0-or-later
/* A disposable provider. No physical NVMEM or flash is accessed. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/slab.h>

#include "test.h"

struct test_context {
	u8 value;
};

static struct test_context *context;
static struct device *parent;
static struct nvmem_device *provider;
static bool dynamic;
module_param(dynamic, bool, 0400);
extern unsigned char layout_fixture_dtb[];
extern unsigned int layout_fixture_dtb_len;

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

static int layout_add_cells(struct nvmem_layout *layout)
{
	struct test_context *ctx = devm_kzalloc(&layout->dev, sizeof(*ctx), GFP_KERNEL);
	struct nvmem_cell_info cell = {
		.name = "token", .offset = 0, .bytes = 1,
		.read_post_process = test_postprocess, .priv = ctx,
	};

	if (!ctx)
		return -ENOMEM;
	ctx->value = 0xa5;
	return nvmem_add_one_cell(layout->nvmem, &cell);
}

static int layout_probe(struct nvmem_layout *layout)
{
	layout->add_cells = layout_add_cells;
	return nvmem_layout_register(layout);
}

static void layout_remove(struct nvmem_layout *layout)
{
	nvmem_layout_unregister(layout);
	pr_info("NVMEM-DETACH: layout disconnected\n");
}

static const struct of_device_id layout_match[] = {
	{ .compatible = "test,detach-layout" }, {}
};
static struct nvmem_layout_driver layout_driver = {
	.driver = { .name = "detach-layout", .of_match_table = layout_match },
	.probe = layout_probe,
	.remove = layout_remove,
};

static int prepare_layout(struct nvmem_config *config)
{
	struct device_node *root;
	void *blob;

	/* Private fixture memory lives until this disposable VM exits. */
	blob = kmemdup(layout_fixture_dtb, layout_fixture_dtb_len, GFP_KERNEL);
	if (!blob)
		return -ENOMEM;
	if (!of_fdt_unflatten_tree(blob, NULL, &root)) {
		kfree(blob);
		return -ENOMEM;
	}
	config->of_node = of_get_child_by_name(root, "provider");
	if (!config->of_node)
		return -ENOENT;
	config->cells = NULL;
	config->ncells = 0;
	return nvmem_layout_driver_register(&layout_driver);
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
	if (dynamic) {
		ret = prepare_layout(&config);
		if (ret) {
			of_node_put(config.of_node);
			kfree(context);
			goto unregister_parent;
		}
	}
	provider = nvmem_register(&config);
	of_node_put(config.of_node);
	if (IS_ERR(provider)) {
		ret = PTR_ERR(provider);
		if (dynamic)
			nvmem_layout_driver_unregister(&layout_driver);
		kfree(context);
		goto unregister_parent;
	}
	return 0;

unregister_parent:
	pr_err("NVMEM-DETACH: provider initialization failed: %d\n", ret);
	root_device_unregister(parent);
	return ret;
}

static void __exit provider_exit(void)
{
	pr_info("NVMEM-DETACH: unregister entered\n");
	nvmem_unregister(provider);
	if (dynamic)
		nvmem_layout_driver_unregister(&layout_driver);
	kfree(context);
	root_device_unregister(parent);
	pr_info("NVMEM-DETACH: provider context freed\n");
}

module_init(provider_init);
module_exit(provider_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Disposable NVMEM provider for disconnect testing");
