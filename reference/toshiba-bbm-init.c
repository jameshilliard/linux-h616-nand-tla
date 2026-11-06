// SPDX-License-Identifier: GPL-2.0
/* Exact function excerpt from drivers/mtd/nand/raw/nand_toshiba.c. */
static int tc58teg5dclta00_init(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	chip->ops.choose_interface_config =
		&tc58teg5dclta00_choose_interface_config;
	chip->options |= NAND_NEED_SCRAMBLING;
	mtd_set_pairing_scheme(mtd, &dist3_pairing_scheme);

	return 0;
}
