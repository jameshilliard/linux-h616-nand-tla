/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Only the 512+16 x8 Hamming initialization is executed. No data I/O. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include "marker_fallback.h"

enum { NAND_ECC_ENGINE_TYPE_ON_HOST, NAND_ECC_ENGINE_TYPE_SOFT, NAND_ECC_ALGO_HAMMING };
enum { SMALL_PAGE = 1, CONTROLLER, UNUSED_LARGE_PAGE };
struct nand_ecc_props { unsigned engine_type, algo, strength, step_size; };
struct mtd_info { unsigned writesize, oobsize; int ooblayout; };
struct mtd_oob_region { unsigned offset, length; };
struct nand_device {
    struct { struct nand_ecc_props user_conf;
             struct { struct nand_ecc_props conf; void *priv; unsigned nsteps, total; } ctx;
    } ecc;
    struct mtd_info *mtd;
};
struct nand_ecc_ctrl { unsigned engine_type, algo, strength, size, steps, total, bytes, options; };
struct nand_chip { struct nand_device base; struct nand_ecc_ctrl ecc; };
struct nand_ecc_sw_hamming_conf { int req_ctx, code_size, sm_order; void *calc_buf, *code_buf; };
static struct nand_device *active;
#define nanddev_to_mtd(n) ((n)->mtd)
#define mtd_to_nanddev(m) (assert((m) == active->mtd), active)
#define mtd_set_ooblayout(m, ops) ((m)->ooblayout = (ops))
#define nand_get_small_page_ooblayout() SMALL_PAGE
#define nand_get_large_page_hamming_ooblayout() UNUSED_LARGE_PAGE
#define nanddev_get_ecc_nsteps(n) ((n)->ecc.ctx.nsteps)
#define kzalloc_obj(obj) calloc(1, sizeof(obj))
#define kzalloc(n, flags) calloc(1, (n))
#define kfree free
#define nand_ecc_init_req_tweaking(ctx, n) 0
#define nand_ecc_cleanup_req_tweaking(ctx) ((void)0)
#define NAND_ECC_SOFT_HAMMING_SM_ORDER 1
#define ENOTSUPP EOPNOTSUPP
#define fallthrough __attribute__((fallthrough))
#define pr_warn(...) ((void)0)
#define WARN(...) ((void)0)
/* Hardware ops validation and software callback installation do no I/O here. */
#define nand_set_ecc_on_host_ops(chip) 0
#define nand_set_ecc_soft_ops(chip) rawnand_sw_hamming_init(chip)
