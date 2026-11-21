/* SPDX-License-Identifier: GPL-2.0-or-later */
/* One selected, writable SLC target, without partitions or secured regions.
 * Buffer preparation, free-region iteration and callback dispatch are real. */
enum { MTD_OPS_PLACE_OOB, MTD_OPS_AUTO_OOB, MTD_OPS_RAW };
struct mtd_oob_ops {
    unsigned mode, ooboffs;
    size_t len, ooblen, retlen, oobretlen;
    u8 *datbuf, *oobbuf;
};
struct mtd_ooblayout_ops {
    int (*free)(struct mtd_info *, int, struct mtd_oob_region *);
};
#define mtd_get_master(m) (m)
#define BUG_ON(x) assert(!(x))
#define BUG() abort()
#define pr_notice(...) ((void)0)
#define pr_debug(...) ((void)0)
#define virt_addr_valid(p) true
static bool target_selected;
static void nand_select_target(struct nand_chip *chip, int target)
{
    assert(!target_selected && !target);
    target_selected = true;
}
static void nand_deselect_target(struct nand_chip *chip)
{
    assert(target_selected);
    target_selected = false;
}
static bool nand_region_is_secured(struct nand_chip *chip, loff_t off, size_t len) { return false; }
static int nand_check_wp(struct nand_chip *chip) { assert(target_selected); return 0; }
