int fdtdec_get_is_enabled(const void *blob, int node)
{
	const char *cell;

	/*
	 * It should say "okay", so only allow that. Some fdts use "ok" but
	 * this is a bug. Please fix your device tree source file. See here
	 * for discussion:
	 *
	 * http://www.mail-archive.com/u-boot@lists.denx.de/msg71598.html
	 */
	cell = fdt_getprop(blob, node, "status", NULL);
	if (cell)
		return strcmp(cell, "okay") == 0;
	return 1;
}
u64 fdtdec_get_number(const fdt32_t *ptr, unsigned int cells)
{
	u64 number = 0;

	while (cells--)
		number = (number << 32) | fdt32_to_cpu(*ptr++);

	return number;
}
u32 fdt_getprop_u32_default_node(const void *fdt, int off, int cell,
				const char *prop, const u32 dflt)
{
	const fdt32_t *val;
	int len;

	val = fdt_getprop(fdt, off, prop, &len);

	/* Check if property exists */
	if (!val)
		return dflt;

	/* Check if property is long enough */
	if (len < ((cell + 1) * sizeof(uint32_t)))
		return dflt;

	return fdt32_to_cpu(*val);
}
int fdt_increase_size(void *fdt, int add_len)
{
	int newlen;

	newlen = fdt_totalsize(fdt) + add_len;

	/* Open in place with a new len */
	return fdt_open_into(fdt, fdt, newlen);
}
static int fdt_del_subnodes(const void *blob, int parent_offset)
{
	int off, ndepth;
	int ret;

	for (ndepth = 0, off = fdt_next_node(blob, parent_offset, &ndepth);
	     (off >= 0) && (ndepth > 0);
	     off = fdt_next_node(blob, off, &ndepth)) {
		if (ndepth == 1) {
			debug("delete %s: offset: %x\n",
				fdt_get_name(blob, off, 0), off);
			ret = fdt_del_node((void *)blob, off);
			if (ret < 0) {
				printf("Can't delete node: %s\n",
					fdt_strerror(ret));
				return ret;
			} else {
				ndepth = 0;
				off = parent_offset;
			}
		}
	}
	return 0;
}

static int fdt_get_partition_parent(const void *blob, int parent_offset)
{
	int off, partitions;

	if (!fdt_node_check_compatible(blob, parent_offset, "fixed-partitions"))
		return parent_offset;

	partitions = fdt_subnode_offset(blob, parent_offset, "partitions");
	if (partitions != -FDT_ERR_NOTFOUND)
		return partitions;

	/* A NAND controller may contain a chip with its own partition table. */
	fdt_for_each_subnode(off, blob, parent_offset) {
		partitions = fdt_subnode_offset(blob, off, "partitions");
		if (partitions != -FDT_ERR_NOTFOUND)
			return partitions;
	}

	/* Legacy layouts put partitions directly below the flash or chip. */
	off = fdt_first_subnode(blob, parent_offset);
	if (off == -FDT_ERR_NOTFOUND)
		return parent_offset;
	if (off < 0)
		return off;
	if (fdt_getprop(blob, off, "label", NULL))
		return parent_offset;

	return fdt_get_partition_parent(blob, off);
}

static int fdt_node_set_part_info(void *blob, int parent_offset,
				  struct mtd_device *dev)
{
	struct list_head *pentry;
	struct part_info *part;
	int part_num, ret;
	int sizecell;
	char buf[64];

	parent_offset = fdt_get_partition_parent(blob, parent_offset);
	if (parent_offset < 0)
		return parent_offset;

	ret = fdt_del_subnodes(blob, parent_offset);
	if (ret < 0)
		return ret;

	/*
	 * Check if size/address is 1 or 2 cells.
	 * We assume #address-cells and #size-cells have same value.
	 */
	sizecell = fdt_getprop_u32_default_node(blob, parent_offset,
						0, "#size-cells", 1);

	part_num = 0;
	list_for_each_prev(pentry, &dev->parts) {
		int newoff;

		part = list_entry(pentry, struct part_info, link);

		debug("%2d: %-20s0x%08llx\t0x%08llx\t%d\n",
			part_num, part->name, part->size,
			part->offset, part->mask_flags);

		sprintf(buf, "partition@%llx", part->offset);
add_sub:
		ret = fdt_add_subnode(blob, parent_offset, buf);
		if (ret == -FDT_ERR_NOSPACE) {
			ret = fdt_increase_size(blob, 512);
			if (!ret)
				goto add_sub;
			else
				goto err_size;
		} else if (ret < 0) {
			printf("Can't add partition node: %s\n",
				fdt_strerror(ret));
			return ret;
		}
		newoff = ret;

		/* Check MTD_WRITEABLE_CMD flag */
		if (part->mask_flags & 1) {
add_ro:
			ret = fdt_setprop(blob, newoff, "read-only", NULL, 0);
			if (ret == -FDT_ERR_NOSPACE) {
				ret = fdt_increase_size(blob, 512);
				if (!ret)
					goto add_ro;
				else
					goto err_size;
			} else if (ret < 0)
				goto err_prop;
		}

add_reg:
		if (sizecell == 2) {
			ret = fdt_setprop_u64(blob, newoff,
					      "reg", part->offset);
			if (!ret)
				ret = fdt_appendprop_u64(blob, newoff,
							 "reg", part->size);
		} else {
			ret = fdt_setprop_u32(blob, newoff,
					      "reg", part->offset);
			if (!ret)
				ret = fdt_appendprop_u32(blob, newoff,
							 "reg", part->size);
		}

		if (ret == -FDT_ERR_NOSPACE) {
			ret = fdt_increase_size(blob, 512);
			if (!ret)
				goto add_reg;
			else
				goto err_size;
		} else if (ret < 0)
			goto err_prop;

add_label:
		ret = fdt_setprop_string(blob, newoff, "label", part->name);
		if (ret == -FDT_ERR_NOSPACE) {
			ret = fdt_increase_size(blob, 512);
			if (!ret)
				goto add_label;
			else
				goto err_size;
		} else if (ret < 0)
			goto err_prop;

		part_num++;
	}
	return 0;
err_size:
	printf("Can't increase blob size: %s\n", fdt_strerror(ret));
	return ret;
err_prop:
	printf("Can't add property: %s\n", fdt_strerror(ret));
	return ret;
}

/*
 * Update partitions in nor/nand nodes using info from
 * mtdparts environment variable. The nodes to update are
 * specified by node_info structure which contains mtd device
 * type and compatible string: E. g. the board code in
 * ft_board_setup() could use:
 *
 *	struct node_info nodes[] = {
 *		{ "fsl,mpc5121-nfc",    MTD_DEV_TYPE_NAND, },
 *		{ "cfi-flash",          MTD_DEV_TYPE_NOR,  },
 *	};
 *
 *	fdt_fixup_mtdparts(blob, nodes, ARRAY_SIZE(nodes));
 */
void fdt_fixup_mtdparts(void *blob, const struct node_info *node_info,
			int node_info_size)
{
	struct mtd_device *dev;
	int i, idx;
	int noff;
	bool inited = false;

	for (i = 0; i < node_info_size; i++) {
		idx = 0;

		fdt_for_each_node_by_compatible(noff, blob, -1,
						node_info[i].compat) {
			const char *prop;

			prop = fdt_getprop(blob, noff, "status", NULL);
			if (prop && !strcmp(prop, "disabled"))
				continue;

			debug("%s: %s, mtd dev type %d\n",
				fdt_get_name(blob, noff, 0),
				node_info[i].compat, node_info[i].type);

			if (!inited) {
				if (mtdparts_init() != 0)
					return;
				inited = true;
			}

			dev = device_find(node_info[i].type, idx++);
			if (dev) {
				if (fdt_node_set_part_info(blob, noff, dev))
					return; /* return on error */
			}
		}
	}
}
static int sunxi_fdt_prepare_fixed_partitions(void *blob,
					      const struct node_info *nodes,
					      int node_count)
{
	int chip, i, nand, partitions, ret;

	if (!IS_ENABLED(CONFIG_SUNXI_NAND_SECURE_STORAGE_MTD_PARTS))
		return 0;

	for (i = 0; i < node_count; i++) {
		fdt_for_each_node_by_compatible(nand, blob, -1,
						nodes[i].compat) {
			const char *status;

			status = fdt_getprop(blob, nand, "status", NULL);
			if (status && !strcmp(status, "disabled"))
				continue;

			fdt_for_each_subnode(chip, blob, nand) {
				const char *name = fdt_get_name(blob, chip, NULL);
				u32 address_cells, size_cells;

				if (!name || strncmp(name, "nand@", 5))
					continue;

				address_cells =
					fdt_getprop_u32_default_node(blob, chip, 0,
								     "#address-cells", 1);
				size_cells =
					fdt_getprop_u32_default_node(blob, chip, 0,
								     "#size-cells", 1);

				ret = fdt_increase_size(blob, 128);
				if (ret)
					return ret;

				partitions =
					fdt_find_or_add_subnode(blob, chip, "partitions");
				if (partitions < 0)
					return partitions;
				address_cells =
					fdt_getprop_u32_default_node(blob, partitions, 0,
								     "#address-cells",
								     address_cells);
				size_cells =
					fdt_getprop_u32_default_node(blob, partitions, 0,
								     "#size-cells", size_cells);

				ret = fdt_setprop_string(blob, partitions, "compatible",
							 "fixed-partitions");
				if (!ret)
					ret = fdt_setprop_u32(blob, partitions,
							      "#address-cells",
							      address_cells);
				if (!ret)
					ret = fdt_setprop_u32(blob, partitions,
							      "#size-cells",
							      size_cells);

				return ret;
			}
		}
	}

	return -FDT_ERR_NOTFOUND;
}

static int sunxi_fdt_mark_ubi_partition(void *blob, int partitions)
{
	int part, ret;

	fdt_for_each_subnode(part, blob, partitions) {
		const char *label = fdt_getprop(blob, part, "label", NULL);

		if (!label || strcmp(label, "UBI") ||
		    !fdt_node_check_compatible(blob, part, "allwinner,sun50i-h616-nand-boot0"))
			continue;

		ret = fdt_setprop_string(blob, part, "compatible", "linux,ubi");
		if (ret == -FDT_ERR_NOSPACE) {
			ret = fdt_increase_size(blob, 32);
			if (!ret)
				ret = fdt_setprop_string(blob, part, "compatible",
							 "linux,ubi");
		}
		return ret;
	}

	return -FDT_ERR_NOTFOUND;
}

static int sunxi_fdt_nand_range(const void *blob, int node, u64 *start, u64 *size)
{
	const fdt32_t *reg;
	int parent, na, ns, len;

	parent = fdt_parent_offset(blob, node);
	if (parent < 0)
		return parent;
	na = fdt_address_cells(blob, parent);
	ns = fdt_size_cells(blob, parent);
	if (na < 1 || na > 2 || ns < 1 || ns > 2)
		return -FDT_ERR_BADNCELLS;
	reg = fdt_getprop(blob, node, "reg", &len);
	if (!reg || len != (na + ns) * sizeof(*reg))
		return -FDT_ERR_BADVALUE;

	/* These are NAND offsets, not CPU addresses to translate. */
	*start = fdtdec_get_number(reg, na);
	*size = fdtdec_get_number(reg + na, ns);
	if (!*size || *size > ~0ULL - *start)
		return -FDT_ERR_BADVALUE;
	return 0;
}

static int sunxi_fdt_remove_boot0_partitions(void *blob, int chip, int partitions)
{
	bool has_boot0 = false;
	int areas, boot, part, ret;

restart:
	areas = fdt_subnode_offset(blob, chip, "boot-areas");
	if (areas == -FDT_ERR_NOTFOUND)
		return 0;
	if (areas < 0)
		return areas;
	if (!fdtdec_get_is_enabled(blob, areas))
		return 0;

	fdt_for_each_subnode(boot, blob, areas) {
		u64 boot_start, boot_size;

		if (!fdtdec_get_is_enabled(blob, boot) ||
		    fdt_node_check_compatible(blob, boot, "allwinner,sun50i-h616-nand-boot0"))
			continue;
		has_boot0 = true;
		if (fdt_address_cells(blob, areas) != 1 || fdt_size_cells(blob, areas) != 1)
			return -FDT_ERR_BADNCELLS;
		ret = sunxi_fdt_nand_range(blob, boot, &boot_start, &boot_size);
		if (ret)
			return ret;

		fdt_for_each_subnode(part, blob, partitions) {
			u64 start, size;

			ret = sunxi_fdt_nand_range(blob, part, &start, &size);
			if (ret)
				return ret;
			if (start >= boot_start + boot_size || boot_start >= start + size)
				continue;
			if (start != boot_start || size != boot_size)
				return -FDT_ERR_BADVALUE;

			ret = fdt_del_node(blob, part);
			if (ret)
				return ret;
			/* Deletion can move the boot-areas sibling. */
			goto restart;
		}
	}

	/* An empty partition table would expose the whole native chip in Linux. */
	if (has_boot0 && fdt_first_subnode(blob, partitions) == -FDT_ERR_NOTFOUND)
		return -FDT_ERR_BADVALUE;
	return 0;
}

static int sunxi_fdt_restore_boot0_partitions(void *blob, const void *original,
					      int chip, int partitions)
{
	char path[256];
	int old_chip, old_parts, boot, earlier, part, prop, ret;

	ret = fdt_get_path(blob, chip, path, sizeof(path));
	if (ret)
		return ret;
	old_chip = fdt_path_offset(original, path);
	if (old_chip < 0)
		return old_chip;
	old_parts = fdt_subnode_offset(original, old_chip, "partitions");
	if (old_parts == -FDT_ERR_NOTFOUND)
		return 0;
	if (old_parts < 0)
		return old_parts;

	fdt_for_each_subnode(boot, original, old_parts) {
		u64 boot_start, boot_size;
		bool found = false;

		if (fdt_node_check_compatible(original, boot, "allwinner,sun50i-h616-nand-boot0"))
			continue;
		ret = sunxi_fdt_nand_range(original, boot, &boot_start, &boot_size);
		if (ret)
			return ret;
		fdt_for_each_subnode(earlier, original, old_parts) {
			u64 start, size;

			if (earlier == boot)
				break;
			if (fdt_node_check_compatible(original, earlier,
						      "allwinner,sun50i-h616-nand-boot0"))
				continue;
			ret = sunxi_fdt_nand_range(original, earlier, &start, &size);
			if (ret)
				return ret;
			if (start < boot_start + boot_size && boot_start < start + size)
				return -FDT_ERR_BADVALUE;
		}
		fdt_for_each_subnode(part, blob, partitions) {
			u64 start, size;

			ret = sunxi_fdt_nand_range(blob, part, &start, &size);
			if (ret)
				return ret;
			if (start >= boot_start + boot_size || boot_start >= start + size)
				continue;
			if (start != boot_start || size != boot_size || found)
				return -FDT_ERR_BADVALUE;
			found = true;

			fdt_for_each_property_offset(prop, original, boot) {
				const char *name;
				const void *value;
				int len;

				value = fdt_getprop_by_offset(original, prop, &name, &len);
				if (!value)
					return len;
				/* Keep the cell encoding of the new partition table. */
				if (!strcmp(name, "reg") || !strcmp(name, "name"))
					continue;
				ret = fdt_setprop(blob, part, name, value, len);
				if (ret == -FDT_ERR_NOSPACE) {
					ret = fdt_increase_size(blob, len + strlen(name) + 32);
					if (!ret)
						ret = fdt_setprop(blob, part, name, value, len);
				}
				if (ret)
					return ret;
			}
		}
		if (!found)
			return -FDT_ERR_BADVALUE;
	}
	return 0;
}

static int sunxi_fdt_finalize_nand_partitions(void *blob,
					      const void *original,
					      const struct node_info *nodes,
					      int node_count)
{
	bool found = false;
	int chip, i, nand, partitions, ret;

	if (!IS_ENABLED(CONFIG_SUNXI_NAND_SECURE_STORAGE_MTD_PARTS))
		return 0;

	for (i = 0; i < node_count; i++) {
		fdt_for_each_node_by_compatible(nand, blob, -1,
						nodes[i].compat) {
			if (!fdtdec_get_is_enabled(blob, nand))
				continue;

			fdt_for_each_subnode(chip, blob, nand) {
				const char *name = fdt_get_name(blob, chip, NULL);

				if (!name || strncmp(name, "nand@", 5) ||
				    !fdtdec_get_is_enabled(blob, chip))
					continue;

				partitions = fdt_subnode_offset(blob, chip, "partitions");
				if (partitions == -FDT_ERR_NOTFOUND)
					continue;
				if (partitions < 0)
					return partitions;

				ret = sunxi_fdt_restore_boot0_partitions(blob, original,
									 chip, partitions);
				if (ret)
					return ret;
				ret = sunxi_fdt_remove_boot0_partitions(blob, chip, partitions);
				if (ret)
					return ret;
				ret = sunxi_fdt_mark_ubi_partition(blob, partitions);
				if (ret == -FDT_ERR_NOTFOUND)
					continue;
				if (ret)
					return ret;
				found = true;
			}
		}
	}

	return found ? 0 : -FDT_ERR_NOTFOUND;
}

static int sunxi_fdt_fixup_nand_partitions(void *blob, const struct node_info *nodes,
					   int node_count)
{
	void *original;
	int ret, size = fdt_totalsize(blob);

	/* The generic fixup replaces nodes, including their format properties. */
	original = malloc(size);
	if (!original)
		return -ENOMEM;
	memcpy(original, blob, size);
	ret = sunxi_fdt_prepare_fixed_partitions(blob, nodes, node_count);
	if (ret && ret != -FDT_ERR_NOTFOUND)
		goto out;
	fdt_fixup_mtdparts(blob, nodes, node_count);
	ret = sunxi_fdt_finalize_nand_partitions(blob, original, nodes, node_count);
	if (ret == -FDT_ERR_NOTFOUND)
		ret = 0;
out:
	if (ret)
		memcpy(blob, original, size);
	free(original);
	return ret;
}
