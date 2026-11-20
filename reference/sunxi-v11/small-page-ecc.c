/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linux ec1e844b9486:
 * ECC layout: Copyright (C) 2019 Macronix. Author: Miquèl Raynal.
 * Hamming setup: Copyright © 2008 Koninklijke Philips Electronics NV.
 * Author: Frans Meulenbroeks.
 */
/* drivers/mtd/nand/ecc.c */
static int nand_ooblayout_ecc_sp(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	struct nand_device *nand = mtd_to_nanddev(mtd);
	unsigned int total_ecc_bytes = nand->ecc.ctx.total;

	if (section > 1)
		return -ERANGE;

	if (!section) {
		oobregion->offset = 0;
		if (mtd->oobsize == 16)
			oobregion->length = 4;
		else
			oobregion->length = 3;
	} else {
		if (mtd->oobsize == 8)
			return -ERANGE;

		oobregion->offset = 6;
		oobregion->length = total_ecc_bytes - 4;
	}

	return 0;
}

/* drivers/mtd/nand/ecc-sw-hamming.c */
int nand_ecc_sw_hamming_init_ctx(struct nand_device *nand)
{
	struct nand_ecc_props *conf = &nand->ecc.ctx.conf;
	struct nand_ecc_sw_hamming_conf *engine_conf;
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int ret;

	if (!mtd->ooblayout) {
		switch (mtd->oobsize) {
		case 8:
		case 16:
			mtd_set_ooblayout(mtd, nand_get_small_page_ooblayout());
			break;
		case 64:
		case 128:
			mtd_set_ooblayout(mtd,
					  nand_get_large_page_hamming_ooblayout());
			break;
		default:
			return -ENOTSUPP;
		}
	}

	conf->engine_type = NAND_ECC_ENGINE_TYPE_SOFT;
	conf->algo = NAND_ECC_ALGO_HAMMING;
	conf->step_size = nand->ecc.user_conf.step_size;
	conf->strength = 1;

	/* Use the strongest configuration by default */
	if (conf->step_size != 256 && conf->step_size != 512)
		conf->step_size = 256;

	engine_conf = kzalloc_obj(*engine_conf);
	if (!engine_conf)
		return -ENOMEM;

	ret = nand_ecc_init_req_tweaking(&engine_conf->req_ctx, nand);
	if (ret)
		goto free_engine_conf;

	engine_conf->code_size = 3;
	engine_conf->calc_buf = kzalloc(mtd->oobsize, GFP_KERNEL);
	engine_conf->code_buf = kzalloc(mtd->oobsize, GFP_KERNEL);
	if (!engine_conf->calc_buf || !engine_conf->code_buf) {
		ret = -ENOMEM;
		goto free_bufs;
	}

	nand->ecc.ctx.priv = engine_conf;
	nand->ecc.ctx.nsteps = mtd->writesize / conf->step_size;
	nand->ecc.ctx.total = nand->ecc.ctx.nsteps * engine_conf->code_size;

	return 0;

free_bufs:
	nand_ecc_cleanup_req_tweaking(&engine_conf->req_ctx);
	kfree(engine_conf->calc_buf);
	kfree(engine_conf->code_buf);
free_engine_conf:
	kfree(engine_conf);

	return ret;
}
