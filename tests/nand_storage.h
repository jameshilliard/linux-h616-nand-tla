/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENOTSUPP 524
#define GFP_KERNEL 0
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define struct_size(p,m,n) (sizeof(*(p)) + sizeof((p)->m[0]) * (n))
#define EXPORT_SYMBOL_GPL(x)
static int core_module;
#define THIS_MODULE (&core_module)
struct kref { unsigned count; };
struct lock { bool held; };
struct device_node { unsigned refs; };
struct device {
    unsigned refs;
    void (*action)(void *);
    void *data;
};
struct nand_storage { struct kref refcnt; void *allocation; };
struct nand_controller { struct lock lock; struct nand_storage storage; };
struct mtd_oob_region { unsigned offset, length; };
struct mtd_info;
struct mtd_ooblayout_ops {
    int (*ecc)(struct mtd_info *, int, struct mtd_oob_region *);
    int (*free)(struct mtd_info *, int, struct mtd_oob_region *);
};
struct mtd_info {
    const char *name;
    struct device *parent_dev;
    struct device_node *node;
    void *owner;
    int (*_get_device)(struct mtd_info *);
    const struct mtd_ooblayout_ops *ooblayout;
    void (*release)(struct mtd_info *);
};
struct nand_lifetime;
struct nand_chip {
    struct mtd_info mtd;
    struct nand_controller *controller;
    struct nand_lifetime *lifetime;
    struct lock lock;
    bool disconnected;
};
static void kref_init(struct kref *ref) { ref->count = 1; }
static void kref_get(struct kref *ref) { assert(ref->count); ref->count++; }
static void kref_put(struct kref *ref, void (*release)(struct kref *))
{ assert(ref->count); if (!--ref->count) release(ref); }
static struct lock *lock_get(struct lock *lock)
{ assert(!lock->held); lock->held = true; return lock; }
static void lock_put(struct lock **lock) { assert((*lock)->held); (*lock)->held = false; }
#define guard(type) struct lock *held __attribute__((cleanup(lock_put))) = lock_get
static int allocation_fail, allocation_index;
static bool action_fail;
static unsigned allocations;
static void *allocate(size_t size)
{
    if (++allocation_index == allocation_fail) return NULL;
    void *p = calloc(1, size); assert(p); allocations++; return p;
}
static void *kzalloc(size_t size, int flags) { return allocate(size); }
static char *kstrdup(const char *str, int flags)
{ char *p = allocate(strlen(str) + 1); if (p) strcpy(p, str); return p; }
static void kfree(const void *ptr)
{ if (ptr) { assert(allocations); allocations--; free((void *)ptr); } }
static void get_device(struct device *dev) { if (dev) dev->refs++; }
static void put_device(struct device *dev) { if (dev) { assert(dev->refs); dev->refs--; } }
static void of_node_get(struct device_node *node) { if (node) node->refs++; }
static void of_node_put(struct device_node *node) { if (node) { assert(node->refs); node->refs--; } }
static const char *dev_name(struct device *dev) { assert(dev); return "test-controller"; }
#define nand_to_mtd(chip) (&(chip)->mtd)
#define mtd_to_nand(p) container_of(p,struct nand_chip,mtd)
#define mtd_get_master(mtd) (mtd)
#define mtd_get_of_node(mtd) ((mtd)->node)
#define nand_get_flash_node(chip) ((chip)->mtd.node)
static void mtd_set_release(struct mtd_info *mtd, void (*release)(struct mtd_info *))
{ mtd->release = release; }
static void mtd_set_ooblayout(struct mtd_info *mtd, const struct mtd_ooblayout_ops *ops)
{ mtd->ooblayout = ops; }
static int layout_query(struct mtd_info *mtd, int section, struct mtd_oob_region *region, bool ecc)
{
    memset(region, 0, sizeof(*region));
    if (section < 0) return -EINVAL;
    if (!mtd->ooblayout) return -ENOTSUPP;
    int (*fn)(struct mtd_info *, int, struct mtd_oob_region *) =
        ecc ? mtd->ooblayout->ecc : mtd->ooblayout->free;
    return fn ? fn(mtd, section, region) : -ENOTSUPP;
}
static int mtd_ooblayout_ecc(struct mtd_info *mtd, int section, struct mtd_oob_region *region)
{ return layout_query(mtd, section, region, true); }
static int mtd_ooblayout_free(struct mtd_info *mtd, int section, struct mtd_oob_region *region)
{ return layout_query(mtd, section, region, false); }
static void nand_controller_init(struct nand_controller *controller) { controller->lock.held = false; }
static int devm_add_action_or_reset(struct device *dev, void (*action)(void *), void *data)
{
    if (action_fail) { action(data); return -ENOMEM; }
    assert(!dev->action); dev->action = action; dev->data = data; return 0;
}
static void devres_release(struct device *dev)
{ dev->action(dev->data); dev->action = NULL; dev->data = NULL; }
