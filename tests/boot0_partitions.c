/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
#define GFP_KERNEL 0
#define MTD_NANDFLASH 4
#define MTD_WRITEABLE 0x400
#define MTD_BIT_WRITEABLE 0x800
#define NAND_BUSWIDTH_16 2
#define THIS_MODULE NULL
#define SUNXI_BOOT0_PAGE_SIZE 2048
#define SUNXI_BOOT0_DATA_SIZE 1024
#define SUNXI_BOOT0_ECC_STRENGTH 80
#define NFC_ECC_EN 1
#define NFC_ECC_PIPELINE 2
#define NFC_ECC_MODE(nfc, mode) ((mode) << 4)
#define NFC_RANDOM_EN(nfc) 4
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))

struct device_node {
    const char *compatible;
    bool enabled;
    int refs;
    struct device_node *children, *next;
};
struct device { struct device_node *of_node; struct device *parent; };
struct mtd_info {
    const char *name;
    void *owner;
    struct device dev;
    int type, flags, ecc_strength, ecc_step_size;
    u64 size;
    unsigned writesize, erasesize, oobsize, writebufsize;
    int (*_read_oob)(void), (*_write_oob)(void), (*_erase)(void);
    int (*_block_isbad)(void), (*_get_device)(void);
    void (*release)(struct mtd_info *);
};
struct mtd_partition {
    const char *name;
    const char *const *types;
    u64 size, offset;
    unsigned mask_flags, add_flags;
    struct device_node *of_node;
};
struct mtd_partitions { const struct mtd_partition *parts; int nr_parts; };
struct sunxi_nfc_caps { int nstrengths; const int *ecc_strengths; };
struct sunxi_nfc { struct device *dev; const struct sunxi_nfc_caps *caps; };
struct nand_chip {
    struct sunxi_nfc *controller;
    struct mtd_info mtd;
    struct device_node *np;
    int options, base;
};
struct list_head { struct list_head *next, *prev; };
struct sunxi_boot0 {
    struct nand_chip *nand;
    struct mtd_info mtd;
    struct list_head node;
    int lock, ecc_ctl;
    u64 offset;
    void *buf;
};
struct sunxi_nand_chip {
    struct nand_chip nand;
    struct list_head boot0_views;
    bool registered;
};

static void *owned[128];
static int live, attempts, fail_alloc, parser_refs;
static int reg_attempts, fail_reg, boot_live, native_calls, legacy_calls, parse_error;
static struct mtd_partition table[16], published[16];
static struct device_node nodes[16], chip_node, container, hw_node;
static int nr_table, nr_published;
static struct mtd_info *native_mtd;
static void *alloc(size_t n)
{
    if (++attempts == fail_alloc) return NULL;
    void *p = calloc(1, n);
    assert(p && live < (int)ARRAY_SIZE(owned));
    owned[live++] = p;
    return p;
}
static void kfree(const void *p)
{
    if (!p) return;
    for (int i = 0; i < live; i++) if (owned[i] == p) {
        free(owned[i]); owned[i] = owned[--live]; return;
    }
    assert(!"invalid free");
}
static char *kstrdup(const char *s, int flags)
{
    assert(s);
    char *p = alloc(strlen(s) + 1);
    if (p) strcpy(p, s);
    return p;
}
#define kmalloc(n, flags) alloc(n)
#define kzalloc_obj(obj) alloc(sizeof(obj))
#define kcalloc(n, size, flags) alloc((n) * (size))
static bool of_device_is_available(struct device_node *np) { return np && np->enabled; }
static bool of_device_is_compatible(struct device_node *np, const char *s)
{ return np && np->compatible && !strcmp(np->compatible, s); }
static struct device_node *of_get_child_by_name(struct device_node *np, const char *s)
{
    assert(!strcmp(s, "partitions"));
    if (np->children) np->children->refs++;
    return np->children;
}
static void put_node(struct device_node **np) { if (*np) assert((*np)->refs-- > 0); }
#define __free(type) __attribute__((cleanup(put_node)))
#define for_each_child_of_node_scoped(np, child) \
    for (struct device_node *child = (np) ? (np)->children : NULL; child; child = child->next)
static struct device_node *nand_get_flash_node(struct nand_chip *nand) { return nand->np; }
static struct mtd_info *nand_to_mtd(struct nand_chip *nand) { return &nand->mtd; }
static struct sunxi_nfc *to_sunxi_nfc(struct sunxi_nfc *nfc) { return nfc; }
static int nanddev_ntargets(int *base) { return *base; }
static unsigned mtd_mod_by_eb(u64 n, struct mtd_info *mtd) { return n % mtd->erasesize; }
static u64 div_u64(u64 n, unsigned d) { return n / d; }
static void mtd_set_of_node(struct mtd_info *mtd, struct device_node *np) { mtd->dev.of_node = np; }
static void mtd_set_release(struct mtd_info *mtd, void (*release)(struct mtd_info *)) { mtd->release = release; }
static void mtd_put_master(struct mtd_info *mtd) { mtd->release(mtd); }
static struct sunxi_boot0 *to_sunxi_boot0(struct mtd_info *mtd) { return container_of(mtd, struct sunxi_boot0, mtd); }
static void mutex_init(int *lock) { *lock = 0; }
#define scoped_guard(type, lock) for (int once = 1; once; once = 0)
static void list_add_tail(struct list_head *n, struct list_head *h)
{ n->prev = h->prev; n->next = h; h->prev->next = n; h->prev = n; }
static void list_del(struct list_head *n) { n->prev->next = n->next; n->next->prev = n->prev; }
#define list_for_each_entry_safe(pos, tmp, h, member) \
    for (struct list_head *cur = (h)->next, *iter_next; \
         cur != (h) && ((iter_next = cur->next), (pos = container_of(cur, struct sunxi_boot0, member)), \
                       (tmp = NULL), (void)(tmp), 1); cur = iter_next)
static int sunxi_boot0_read_oob(void) { return 0; }
static int sunxi_boot0_write_oob(void) { return 0; }
static int sunxi_boot0_erase(void) { return 0; }
static int sunxi_boot0_isbad(void) { return 0; }
static int sunxi_boot0_get_device(void) { return 0; }

static int mtd_parse_partitions(struct mtd_info *mtd, const char *const *types,
                                void *data, struct mtd_partitions *parts)
{
    assert(mtd == native_mtd && !strcmp(types[0], "fixed-partitions") && !types[1]);
    if (parse_error) return parse_error == 1 ? 0 : parse_error;
    struct mtd_partition *copy = alloc(nr_table * sizeof(*copy));
    if (!copy) return -ENOMEM;
    memcpy(copy, table, nr_table * sizeof(*copy));
    parts->parts = copy; parts->nr_parts = nr_table; parser_refs++;
    return nr_table;
}
static void mtd_part_parser_cleanup(struct mtd_partitions *parts)
{ assert(parser_refs-- == 1); kfree(parts->parts); }
static int mtd_device_register(struct mtd_info *mtd, void *parts, int nr)
{ assert(mtd == native_mtd && !nr); legacy_calls++; return 0; }
static int mtd_device_parse_register(struct mtd_info *mtd, const char *const *types,
                                     void *data, const struct mtd_partition *parts, int nr)
{
    assert(types && !*types);
    if (++reg_attempts == fail_reg) return -EIO;
    if (mtd == native_mtd) {
        assert(nr > 0); /* Zero would register an unintended native master. */
        native_calls++; nr_published = nr;
        memcpy(published, parts, nr * sizeof(*parts));
        for (int i = 0; i < nr; i++)
            assert(!of_device_is_compatible(published[i].of_node, "allwinner,sun50i-h616-nand-boot0"));
    } else {
        assert(!nr);
        struct sunxi_boot0 *boot = to_sunxi_boot0(mtd);
        int index = mtd->dev.of_node - nodes;
        assert(index >= 0 && index < nr_table);
        assert(mtd->writesize == 1024 && mtd->oobsize == 1152 && mtd->erasesize == 65536);
        assert(mtd->size == table[index].size / 2 && boot->offset == table[index].offset);
        assert(mtd->flags == (native_mtd->flags & ~(MTD_BIT_WRITEABLE | table[index].mask_flags)));
        boot_live++;
    }
    return 0;
}
static void mtd_device_unregister(struct mtd_info *mtd)
{ assert(mtd != native_mtd && boot_live > 0); boot_live--; }
/* @driver@ */

static const int strengths[] = {16, 32, 80};
static struct sunxi_nfc_caps caps = {3, strengths};
static struct device hwdev;
static struct sunxi_nfc nfc = {&hwdev, &caps};
static struct sunxi_nand_chip chip;
static int cases;
static void setup(int count, unsigned boots)
{
    assert(!live && !boot_live && !parser_refs && !container.refs);
    memset(&chip, 0, sizeof(chip));
    memset(table, 0, sizeof(table)); memset(nodes, 0, sizeof(nodes));
    container = (struct device_node){.enabled = true, .children = nodes};
    chip_node = (struct device_node){.enabled = true, .children = &container};
    hw_node = (struct device_node){.compatible = "allwinner,sun50i-h616-nand-controller", .enabled = true};
    hwdev.of_node = &hw_node;
    chip.nand.controller = &nfc; chip.nand.np = &chip_node; chip.nand.base = 1;
    chip.nand.mtd = (struct mtd_info){.size = 0x10000000, .writesize = 2048,
        .erasesize = 131072, .oobsize = 128, .flags = MTD_WRITEABLE | MTD_BIT_WRITEABLE};
    native_mtd = &chip.nand.mtd;
    chip.boot0_views.next = chip.boot0_views.prev = &chip.boot0_views;
    nr_table = count;
    for (int i = 0; i < count; i++) {
        nodes[i].enabled = true;
        nodes[i].next = i + 1 < count ? &nodes[i + 1] : NULL;
        nodes[i].compatible = (boots & (1U << i)) ? "allwinner,sun50i-h616-nand-boot0" : NULL;
        table[i] = (struct mtd_partition){.offset = i * 0x40000, .size = 0x40000,
            .name = "partition", .of_node = &nodes[i]};
    }
    attempts = fail_alloc = reg_attempts = fail_reg = native_calls = legacy_calls = parse_error = nr_published = 0;
}
static void finish(int expected, int boots, int natives)
{
    int ret = sunxi_nand_register(&chip);
    assert(ret == expected && !parser_refs && !container.refs);
    if (!ret) {
        assert(boot_live == boots);
        assert(nr_published == natives);
        assert(chip.registered == !!(natives || legacy_calls));
    }
    sunxi_boot0_cleanup(&chip);
    assert(!boot_live && !live && !parser_refs);
    cases++;
}
int main(void)
{
    setup(7, 15); finish(0, 4, 3);
    setup(4, 15); finish(0, 4, 0);
    setup(3, 0); finish(0, 0, 0); assert(legacy_calls == 1);
    setup(4, 15); for (int i = 0; i < 4; i++) nodes[i].enabled = false; finish(0, 0, 0);
    setup(7, 15); nodes[0].enabled = false; nodes[6].enabled = false; finish(0, 3, 2);
    setup(7, 15); table[0].mask_flags = MTD_WRITEABLE; finish(0, 4, 3);
    setup(7, 15); nodes[6].compatible = "linux,ubi";
    table[5].mask_flags = MTD_WRITEABLE; finish(0, 4, 3);
    assert(!strcmp(published[2].of_node->compatible, "linux,ubi"));
    assert(published[1].mask_flags == MTD_WRITEABLE);
    setup(7, 15); table[0].add_flags = 1; finish(-EOPNOTSUPP, 0, 0);
    setup(7, 15); table[0].mask_flags = 2; finish(-EOPNOTSUPP, 0, 0);
    setup(7, 15); container.enabled = false; finish(-EINVAL, 0, 0);
    for (int mode = 0; mode < 3; mode++) {
        setup(7, 15);
        if (mode == 0) chip.nand.options = NAND_BUSWIDTH_16;
        if (mode == 1) chip.nand.base = 2;
        if (mode == 2) chip.nand.mtd.writesize = 4096;
        finish(-EOPNOTSUPP, 0, 0);
    }
    for (int i = 0; i < 7; i++) {
        setup(7, 15); table[i].size = 0; finish(-EINVAL, 0, 0);
        setup(7, 15); table[i].offset = UINT64_MAX - 1; finish(-EINVAL, 0, 0);
        setup(7, 15); table[i].size = UINT64_MAX; finish(-EINVAL, 0, 0);
    }
    setup(7, 15); table[0].offset = 1; finish(-EINVAL, 0, 0);
    setup(7, 15); table[0].size--; finish(-EINVAL, 0, 0);
    for (int i = 0; i < 7; i++) for (int j = 0; j < 7; j++) {
        if (i == j || (i >= 4 && j >= 4)) continue;
        setup(7, 15); table[i].offset = table[j].offset; finish(-EINVAL, 0, 0);
    }
    for (int mask = 1; mask < 128; mask++) {
        setup(7, mask); int boots = __builtin_popcount((unsigned)mask);
        finish(0, boots, 7 - boots);
    }
    const int errors[] = {-ENOMEM, -EINVAL, -ENODEV, -517, 1};
    for (unsigned i = 0; i < ARRAY_SIZE(errors); i++) {
        setup(7, 15); parse_error = errors[i];
        finish(errors[i] == 1 ? -EINVAL : errors[i], 0, 0);
        assert(!native_calls && !legacy_calls);
    }
    for (int i = 1; i <= 14; i++) {
        setup(7, 15); fail_alloc = i; finish(-ENOMEM, 0, 0);
    }
    for (int i = 1; i <= 5; i++) {
        setup(7, 15); fail_reg = i; finish(-EIO, 0, 0);
    }
    setup(2, 1); native_mtd->size = 1ULL << 36;
    table[0].offset = 1ULL << 35; finish(0, 1, 1);
    printf("BOOT0 PARTITIONS PASS: %d cases\n", cases);
}
