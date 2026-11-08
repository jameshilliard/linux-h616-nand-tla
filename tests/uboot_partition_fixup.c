/* SPDX-License-Identifier: GPL-2.0+ */
/* Host adapters for the actual U-Boot partition fixup implementation. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

typedef uint32_t u32;
typedef uint8_t u8;
typedef uint64_t u64;
struct list_head { struct list_head *next, *prev; };
struct part_info {
	struct list_head link;
	unsigned long long size, offset;
	unsigned int mask_flags;
	char *name;
};
struct mtd_device { struct list_head parts; };
struct node_info { const char *compat; int type; };
static struct mtd_device device;
#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr) - __builtin_offsetof(type, member)))
#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev; pos != (head); pos = pos->prev)
#define debug(...) do { } while (0)
#define IS_ENABLED(option) 1
#define fdt_for_each_node_by_compatible(node, fdt, start, compat) \
	for (node = fdt_node_offset_by_compatible(fdt, start, compat); node >= 0; \
	     node = fdt_node_offset_by_compatible(fdt, node, compat))
static int mtdparts_init(void) { return 0; }
static struct mtd_device *device_find(u8 type, u8 num)
{
	return num == 0 ? &device : NULL;
}
static int fdt_find_or_add_subnode(void *blob, int parent, const char *name)
{
	int off = fdt_subnode_offset(blob, parent, name);
	return off == -FDT_ERR_NOTFOUND ? fdt_add_subnode(blob, parent, name) : off;
}

#include "fixup-under-test.c"

#ifndef HAVE_BOOT0_FIXUP
#define sunxi_fdt_finalize_nand_partitions sunxi_fdt_set_ubi_compatible
#endif

static unsigned char saved[16384];
static int snapshot(const void *blob, const char *path, bool compare)
{
	int start = fdt_path_offset(blob, path), off, next, depth = 0;
	if (start < 0)
		return start;
	off = start;
	do {
		uint32_t tag = fdt_next_tag(blob, off, &next);
		assert(next >= 0);
		if (tag == FDT_BEGIN_NODE)
			depth++;
		else if (tag == FDT_END_NODE)
			depth--;
		else
			assert(tag != FDT_END);
		off = next;
	} while (depth);
	assert(off - start < sizeof(saved));
	const void *data = fdt_offset_ptr(blob, start, off - start);
	assert(data);
	if (compare)
		assert(!memcmp(saved, data, off - start));
	else
		memcpy(saved, data, off - start);
	return off - start;
}

int main(int argc, char **argv)
{
	assert(argc == 9 || argc == 10);
	void *blob = calloc(1, 262144);
	assert(blob);
	FILE *input = fopen(argv[1], "rb");
	assert(input);
	assert(fread(blob, 1, 131072, input) > 0);
	fclose(input);
	assert(!fdt_check_header(blob));
	struct node_info nodes[] = { { argc == 10 ? argv[9] : "test,nand", 0 } };
	struct part_info parts[] = {
		{ .name = "spl-0", .offset = 0, .size = 0x40000 },
		{ .name = "spl-1", .offset = 0x40000, .size = 0x40000 },
		{ .name = "spl-2", .offset = 0x80000, .size = 0x40000 },
		{ .name = "spl-3", .offset = 0xc0000, .size = 0x40000 },
		{ .name = "uboot", .offset = 0x100000, .size = 0x400000 },
		{ .name = "securestorage", .offset = 0x500000, .size = 0x40000, .mask_flags = 1 },
		{ .name = "UBI", .offset = 0x540000, .size = 0xfac0000 },
	};
	device.parts.next = &parts[0].link;
	device.parts.prev = &parts[6].link;
	for (int i = 0; i < 7; i++) {
		parts[i].link.prev = i ? &parts[i - 1].link : &device.parts;
		parts[i].link.next = i < 6 ? &parts[i + 1].link : &device.parts;
	}
	int saved_size = snapshot(blob, argv[4], false);
	unsigned int removed = strtoul(argv[7], NULL, 0);
	bool expect_error = atoi(argv[8]);
#ifdef HAVE_SINGLE_MAP
	bool format[7] = {}, readonly[7] = {}, disabled[7] = {};
	char labels[7][80] = {};
	if (atoi(argv[6]) == 3 && !expect_error) {
		int parent = fdt_path_offset(blob, argv[3]), node;
		fdt_for_each_subnode(node, blob, parent) {
			u64 start, size;
			if (fdt_node_check_compatible(blob, node, "allwinner,sun50i-h616-nand-boot0"))
				continue;
			assert(!sunxi_fdt_nand_range(blob, node, &start, &size));
			int i = start / 0x40000;
			assert(i < 4);
			format[i] = true;
			readonly[i] = fdt_getprop(blob, node, "read-only", NULL) != NULL;
			disabled[i] = !fdtdec_get_is_enabled(blob, node);
			snprintf(labels[i], sizeof(labels[i]), "%s", (const char *)fdt_getprop(blob, node, "label", NULL));
		}
	}
#endif
	for (int pass = 0; pass < 2; pass++) {
#ifdef HAVE_SINGLE_MAP
		void *original = malloc(fdt_totalsize(blob));
		assert(original);
		memcpy(original, blob, fdt_totalsize(blob));
		if (atoi(argv[6]) == 3) {
			int ret = sunxi_fdt_fixup_nand_partitions(blob, nodes, 1);
			if ((ret < 0) != expect_error)
				fprintf(stderr, "fixup returned %d, expected_error=%d, pass=%d\n", ret, expect_error, pass);
			assert((ret < 0) == expect_error);
			goto verify;
		}
#endif
		if (atoi(argv[5]))
			assert(!sunxi_fdt_prepare_fixed_partitions(blob, nodes, 1));
		fdt_fixup_mtdparts(blob, nodes, 1);
		if (atoi(argv[6])) {
			int ret = sunxi_fdt_finalize_nand_partitions(blob,
#ifdef HAVE_SINGLE_MAP
								  original,
#endif
								  nodes, 1);

			assert((ret < 0 && ret != -FDT_ERR_NOTFOUND) == expect_error);
			if (!ret && !expect_error) {
				/* The post-fixup helper alone must also be idempotent. */
				assert(!sunxi_fdt_finalize_nand_partitions(blob,
#ifdef HAVE_SINGLE_MAP
									 original,
#endif
									 nodes, 1));
			}
		}
#ifdef HAVE_SINGLE_MAP
verify:
#endif
		assert(snapshot(blob, argv[4], true) == saved_size);
#ifdef HAVE_SINGLE_MAP
		free(original);
#endif
		if (expect_error)
			continue;
		int parent = fdt_path_offset(blob, argv[3]), off, index = 0, count = 0;
		assert(parent >= 0);
		fdt_for_each_subnode(off, blob, parent) {
			int len;
			int cells = fdt_size_cells(blob, parent);
			const char *label = fdt_getprop(blob, off, "label", NULL);
			const fdt32_t *reg = fdt_getprop(blob, off, "reg", &len);
			while (removed & (1U << index))
				index++;
			assert(index < 7 && label && reg && len == 2 * cells * sizeof(*reg));
#ifdef HAVE_SINGLE_MAP
			if (format[index]) {
				assert(!strcmp(label, labels[index]));
				assert(!fdt_node_check_compatible(blob, off, "allwinner,sun50i-h616-nand-boot0"));
				assert((fdt_getprop(blob, off, "read-only", NULL) != NULL) == readonly[index]);
				assert(!fdtdec_get_is_enabled(blob, off) == disabled[index]);
			} else
#endif
				assert(!strcmp(label, parts[index].name));
			assert(fdtdec_get_number(reg, cells) == parts[index].offset);
			assert(fdtdec_get_number(reg + cells, cells) == parts[index].size);
			if ((atoi(argv[6]) == 1 || atoi(argv[6]) == 3) && index == 6)
				assert(!fdt_node_check_compatible(blob, off, "linux,ubi"));
			if (index == 5)
#ifdef HAVE_READ_ONLY
				assert(fdt_getprop(blob, off, "read-only", NULL));
#else
				assert(fdt_getprop(blob, off, "read_only", NULL));
#endif
			count++;
			index++;
		}
		assert(count == 7 - __builtin_popcount(removed));
	}
	FILE *output = fopen(argv[2], "wb");
	assert(output);
	assert(fwrite(blob, 1, fdt_totalsize(blob), output) == fdt_totalsize(blob));
	fclose(output);
	free(blob);
	puts("PASS: repeated fixup, expected native ranges, preserved sibling subtree");
}
