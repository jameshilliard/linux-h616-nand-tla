/* SPDX-License-Identifier: GPL-2.0-or-later */
struct kref { unsigned n; };
static unsigned releases, cleanup_calls, owner_refs, module_refs, warnings;
static bool partitioned_master, module_available=true;
struct device { bool registered; };
struct mtd_info {
    struct mtd_info *parent;
    struct kref refcnt;
    struct device dev;
    struct { void *dfs_dir; } dbg;
    void *nvmem, *otp_user_nvmem, *otp_factory_nvmem, *owner;
    int (*_get_device)(struct mtd_info *);
    void (*_put_device)(struct mtd_info *), (*_reboot)(struct mtd_info *);
    int reboot_notifier, index;
    const char *name;
    struct list_head partitions;
    struct { struct list_head node; } part;
    struct { struct lock partitions_lock; } master;
};
struct mtd_notifier { struct list_head list; void (*remove)(struct mtd_info *); };
struct nand_chip { struct mtd_info mtd; bool resources; };
struct sunxi_nand_chip { struct nand_chip nand; struct list_head node; };
struct sunxi_nfc { struct list_head chips; };
static struct lock mtd_table_mutex;
static struct mtd_info *mtd_idr[8];
static LIST_HEAD(mtd_notifiers);
static int owner;
#define THIS_MODULE NULL
#define CONFIG_MTD_PARTITIONED_MASTER partitioned_master
#define CONFIG_MTD_VIRT_CONCAT 0
#define IS_ENABLED(x) (x)
#define IS_REACHABLE(x) (x)
#define WARN_ON(x) do { if (x) warnings++; } while (0)
#define nand_to_mtd(c) (&(c)->mtd)
#define mtd_is_partition(m) ((m)->parent!=NULL)
#define mtd_has_partitions(m) (!list_empty(&(m)->partitions))
static void *idr_find(void *idr, int index) { return mtd_idr[index]; }
static void debugfs_remove_recursive(void *p) { }
static void nvmem_unregister(void *p) { }
static bool device_is_registered(struct device *d) { return d->registered; }
static void device_unregister(struct device *d) { assert(d->registered); d->registered=false; releases++; }
static void unregister_reboot_notifier(void *n) { }
static int mtd_virt_concat_destroy(struct mtd_info *m) { assert(0); return 0; }
static bool try_module_get(void *p) { if (!module_available) return false; owner_refs++; return true; }
static void module_put(void *p) { if (p) { assert(owner_refs); owner_refs--; } else { assert(module_refs); module_refs--; } }
static void kref_get(struct kref *r) { assert(r->n); r->n++; }
static void kref_put(struct kref *r, void (*release)(struct kref *))
{ assert(r->n); if (!--r->n) release(r); }
static void nand_cleanup(struct nand_chip *c)
{ assert(c->resources); c->resources=false; cleanup_calls++; }
