/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Narrow MMIO/API fixtures: no NAND discovery, BCH or geometry emulation. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define NAND_NEED_SCRAMBLING 1U
#define NAND_NO_SUBPAGE_WRITE 2U
#define NAND_SUBPAGE_READ 4U
#define NAND_BBT_USE_FLASH 1U
#define NAND_BBT_NO_OOB 2U
#define NAND_ECC_ENGINE_TYPE_ON_HOST 0
#define NAND_ECC_ENGINE_TYPE_NONE 1
#define NAND_ECC_ENGINE_TYPE_SOFT 2
#define USER_DATA_SZ 4U
#define SUNXI_NFC_MAX_USER_DATA_SZ 32U
#define NFC_REG_ECC_CTL 0x34
#define NFC_REG_USER_DATA(n, i) ((n)->caps->reg_user_data + (i) * 4)
#define NFC_RANDOM_EN(n) ((n)->caps->random_en)
#define NFC_RANDOM_SEED_MSK (0x7fffU << 16)
#define NFC_RANDOM_SEED(seed) ((seed) << 16)
#define mtd_div_by_ws(n, m) ((n) / (m)->writesize)
#define dev_err_probe(d, err, ...) ((void)(d), (err))
struct nand_chip;
struct sunxi_nfc_ecc_read;
struct sunxi_nfc_ecc_snapshot;
struct sunxi_nfc_oob_ops;
struct device_node { int unused; };
struct mtd_info { unsigned writesize, erasesize, ecc_step_size; };
struct nand_ecc_props { unsigned step_size, strength; };
struct nand_ecc_ctrl {
    unsigned size, strength;
    int engine_type;
    int (*read_subpage)(struct nand_chip *, u32, u32, u8 *, int);
};
struct caps {
    unsigned reg_user_data, reg_user_data_len, random_en;
    const struct sunxi_nfc_oob_ops *allwinner_oob_ops;
};
struct sunxi_nfc { u8 regs[0x1000]; struct caps *caps; void *dev; };
struct nand_chip {
    struct sunxi_nfc *controller;
    struct mtd_info mtd;
    struct nand_ecc_props base;
    struct nand_ecc_ctrl ecc;
    unsigned options, bbt_options;
    u8 *user_data_bytes;
    const struct sunxi_nfc_oob_ops *oob_ops;
    struct { void (*choose_interface_config)(void); } ops;
};
#define sunxi_nand_chip nand_chip
#define to_sunxi_nand(n) (n)
#define to_sunxi_nfc(n) (n)
#define nand_to_mtd(n) (&(n)->mtd)
static const struct nand_ecc_props *nanddev_get_ecc_requirements(const struct nand_ecc_props *p) { return p; }
static struct device_node *nand_get_flash_node(struct nand_chip *n) { return NULL; }
static bool vendor_property;
static bool of_property_read_bool(struct device_node *n, const char *s) { return vendor_property; }
#if SINGLE_INSTALL
static const struct sunxi_nfc_oob_ops *geometry_ops;
static int sunxi_nand_hw_ecc_ctrl_init(struct nand_chip *n, struct nand_ecc_ctrl *e,
                                     const struct sunxi_nfc_oob_ops *ops)
{ assert(!n->oob_ops && ops); geometry_ops = ops; return 0; }
#else
static int sunxi_nand_hw_ecc_ctrl_init(struct nand_chip *n, struct nand_ecc_ctrl *e, struct device_node *d) { return 0; }
#endif
static void tc58teg5dclta00_choose_interface_config(void) { }
static const int dist3_pairing_scheme;
static void mtd_set_pairing_scheme(struct mtd_info *m, const int *s) { }
static u32 readl(const void *p) { u32 v; memcpy(&v, p, 4); return v; }
static void writel(u32 v, void *p) { memcpy(p, &v, 4); }
static unsigned sunxi_nfc_oob_min_user_data(struct nand_chip *n, int steps) { return steps * 4; }
static int sunxi_nfc_oob_init_user_data(struct nand_chip *n, int steps) { return 0; }
#if HAS_VENDOR
static int sunxi_nfc_oob_read_batch(struct nand_chip *n, struct sunxi_nfc_ecc_read *r, struct sunxi_nfc_ecc_snapshot *s, int f, int count) { return 0; }
#else
static int sunxi_nfc_oob_read_batch(struct nand_chip *n, struct sunxi_nfc_ecc_read *r, u32 s, u32 p, int f, int count) { return 0; }
#endif
static int sunxi_nfc_oob_finish_read(struct nand_chip *n, struct sunxi_nfc_ecc_read *r, int *off) { return 0; }
static unsigned sunxi_nfc_allwinner_oob_min_user_data(struct nand_chip *n, int steps) { return steps * 4; }
static int sunxi_nfc_allwinner_oob_init_user_data(struct nand_chip *n, int steps) { return 0; }
static int sun4i_a10_nfc_allwinner_oob_init_user_data(struct nand_chip *n, int steps) { return 0; }
static unsigned sun50i_h6_nfc_allwinner_oob_min_user_data(struct nand_chip *n, int steps) { return steps * 4; }
static int sun50i_h6_nfc_allwinner_oob_init_user_data(struct nand_chip *n, int steps) { return 0; }
static int sunxi_nfc_allwinner_oob_read_batch(struct nand_chip *n, struct sunxi_nfc_ecc_read *r, struct sunxi_nfc_ecc_snapshot *s, int f, int count) { return 0; }
static int sunxi_nfc_allwinner_oob_finish_read(struct nand_chip *n, struct sunxi_nfc_ecc_read *r, int *off) { return 0; }
static int sunxi_nfc_allwinner_oob_read_subpage(struct nand_chip *n, u32 a, u32 b, u8 *p, int page) { return 0; }
