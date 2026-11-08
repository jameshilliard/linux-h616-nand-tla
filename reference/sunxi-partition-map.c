#line 3390 "sunxi_nand.c"
static void sunxi_boot0_release(struct mtd_info *mtd)
{
	struct sunxi_boot0 *boot = to_sunxi_boot0(mtd);

	kfree(mtd->name);
	kfree(boot->buf);
	kfree(boot);
}
#line 3399 "sunxi_nand.c"
static void sunxi_boot0_cleanup(struct sunxi_nand_chip *chip)
{
	struct sunxi_boot0 *boot, *next;

	list_for_each_entry_safe(boot, next, &chip->boot0_views, node) {
		scoped_guard(mutex, &boot->lock)
			boot->nand = NULL;
		mtd_device_unregister(&boot->mtd);
		list_del(&boot->node);
		mtd_put_master(&boot->mtd);
	}
}
#line 3412 "sunxi_nand.c"
static int sunxi_boot0_register(struct sunxi_nand_chip *chip,
				const struct mtd_partition *part)
{
	static const char * const no_parsers[] = { NULL };
	struct nand_chip *nand = &chip->nand;
	struct mtd_info *native = nand_to_mtd(nand);
	struct sunxi_nfc *nfc = to_sunxi_nfc(nand->controller);
	struct sunxi_boot0 *boot;
	struct mtd_info *mtd;
	int ret, mode;

	/* H616 BootROM format on single-target, 8-bit, 2-KiB-page NAND. */
	if (!of_device_is_compatible(nfc->dev->of_node, "allwinner,sun50i-h616-nand-controller") ||
	    native->writesize != SUNXI_BOOT0_PAGE_SIZE || nanddev_ntargets(&nand->base) != 1 ||
	    nand->options & NAND_BUSWIDTH_16)
		return -EOPNOTSUPP;
	if (mtd_mod_by_eb(part->offset, native) || mtd_mod_by_eb(part->size, native))
		return -EINVAL;
	if (part->add_flags || (part->mask_flags & ~MTD_WRITEABLE))
		return -EOPNOTSUPP;
	for (mode = 0; mode < nfc->caps->nstrengths; mode++)
		if (nfc->caps->ecc_strengths[mode] == SUNXI_BOOT0_ECC_STRENGTH)
			break;
	if (mode == nfc->caps->nstrengths)
		return -EOPNOTSUPP;
	boot = kzalloc_obj(*boot);
	if (!boot)
		return -ENOMEM;
	mtd = &boot->mtd;
	mtd->name = kstrdup(part->name, GFP_KERNEL);
	boot->buf = kmalloc(native->writesize + native->oobsize, GFP_KERNEL);
	if (!mtd->name || !boot->buf) {
		sunxi_boot0_release(mtd);
		return -ENOMEM;
	}
	mutex_init(&boot->lock);
	boot->nand = nand;
	boot->offset = part->offset;
	boot->ecc_ctl = NFC_ECC_EN | NFC_ECC_PIPELINE | NFC_ECC_MODE(nfc, mode) |
			NFC_RANDOM_EN(nfc);
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = nfc->dev;
	mtd_set_of_node(mtd, part->of_node);
	mtd->type = MTD_NANDFLASH;
	mtd->flags = native->flags & ~(MTD_BIT_WRITEABLE | part->mask_flags);
	mtd->size = div_u64(part->size, native->writesize) * SUNXI_BOOT0_DATA_SIZE;
	mtd->erasesize = (native->erasesize / native->writesize) * SUNXI_BOOT0_DATA_SIZE;
	mtd->writesize = SUNXI_BOOT0_DATA_SIZE;
	mtd->writebufsize = mtd->writesize;
	mtd->oobsize = native->writesize + native->oobsize - mtd->writesize;
	mtd->ecc_strength = SUNXI_BOOT0_ECC_STRENGTH;
	mtd->ecc_step_size = SUNXI_BOOT0_DATA_SIZE;
	mtd->_read_oob = sunxi_boot0_read_oob;
	mtd->_write_oob = sunxi_boot0_write_oob;
	mtd->_erase = sunxi_boot0_erase;
	mtd->_block_isbad = sunxi_boot0_isbad;
	mtd->_get_device = sunxi_boot0_get_device;
	mtd_set_release(mtd, sunxi_boot0_release);
	ret = mtd_device_parse_register(mtd, no_parsers, NULL, NULL, 0);
	if (ret) {
		mtd_put_master(mtd);
		return ret;
	}
	list_add_tail(&boot->node, &chip->boot0_views);
	return 0;
}
#line 3479 "sunxi_nand.c"
static bool sunxi_nand_boot0_partition(const struct mtd_partition *part)
{
	return of_device_is_compatible(part->of_node, "allwinner,sun50i-h616-nand-boot0");
}
#line 3484 "sunxi_nand.c"
static int sunxi_nand_has_boot0_partitions(struct nand_chip *chip)
{
	struct device_node *parts __free(device_node) =
		of_get_child_by_name(nand_get_flash_node(chip), "partitions");

	/* Disabled BootROM entries must not become native-format aliases either. */
	for_each_child_of_node_scoped(parts, child)
		if (of_device_is_compatible(child, "allwinner,sun50i-h616-nand-boot0"))
			return of_device_is_available(parts) ? 1 : -EINVAL;

	return 0;
}
#line 3497 "sunxi_nand.c"
static int sunxi_nand_check_partitions(struct mtd_info *mtd,
				       const struct mtd_partitions *parts)
{
	int i, j;

	for (i = 0; i < parts->nr_parts; i++) {
		const struct mtd_partition *part = &parts->parts[i];

		if (!of_device_is_available(part->of_node))
			continue;
		if (!part->size || part->offset >= mtd->size ||
		    part->size > mtd->size - part->offset)
			return -EINVAL;
	}

	for (i = 0; i < parts->nr_parts; i++) {
		const struct mtd_partition *boot = &parts->parts[i];

		if (!of_device_is_available(boot->of_node) || !sunxi_nand_boot0_partition(boot))
			continue;
		for (j = 0; j < parts->nr_parts; j++) {
			const struct mtd_partition *other = &parts->parts[j];

			if (i == j || !of_device_is_available(other->of_node))
				continue;
			if (boot->offset < other->offset + other->size &&
			    other->offset < boot->offset + boot->size)
				return -EINVAL;
		}
	}

	return 0;
}
#line 3531 "sunxi_nand.c"
static int sunxi_nand_register(struct sunxi_nand_chip *chip)
{
	static const char * const fixed_partitions[] = { "fixed-partitions", NULL };
	static const char * const no_parsers[] = { NULL };
	struct mtd_info *mtd = nand_to_mtd(&chip->nand);
	struct mtd_partitions parts = { };
	struct mtd_partition *native;
	int ret, i, nr_native = 0;

	ret = sunxi_nand_has_boot0_partitions(&chip->nand);
	if (ret < 0)
		return ret;
	if (!ret) {
		ret = mtd_device_register(mtd, NULL, 0);
		if (!ret)
			chip->registered = true;
		return ret;
	}

	/* A command-line partition table cannot describe per-partition formats. */
	ret = mtd_parse_partitions(mtd, fixed_partitions, NULL, &parts);
	if (ret <= 0)
		return ret ?: -EINVAL;
	ret = sunxi_nand_check_partitions(mtd, &parts);
	if (ret)
		goto out;
	native = kcalloc(parts.nr_parts, sizeof(*native), GFP_KERNEL);
	if (!native) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < parts.nr_parts; i++) {
		const struct mtd_partition *part = &parts.parts[i];

		if (!of_device_is_available(part->of_node))
			continue;
		if (!sunxi_nand_boot0_partition(part)) {
			native[nr_native++] = *part;
			continue;
		}
		ret = sunxi_boot0_register(chip, part);
		if (ret)
			goto free_native;
	}

	/* Registering zero partitions would expose the whole native device. */
	ret = 0;
	if (nr_native) {
		ret = mtd_device_parse_register(mtd, no_parsers, NULL, native, nr_native);
		if (!ret)
			chip->registered = true;
	}
free_native:
	kfree(native);
out:
	mtd_part_parser_cleanup(&parts);
	return ret;
}
