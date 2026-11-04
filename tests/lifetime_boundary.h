/* SPDX-License-Identifier: GPL-2.0-or-later */
struct kref { unsigned n; };
static unsigned releases, cleanup_calls, owner_refs, module_refs, warnings;
static bool partitioned_master, module_available=true;
struct device { bool registered; };
typedef uint64_t u64;
typedef uint8_t u8;
struct mtd_oob_ops { size_t len, retlen; u8 *datbuf; };
struct mtd_info {
    struct mtd_info *parent;
    struct kref refcnt;
    struct device dev;
    struct { void *dfs_dir; } dbg;
    void *nvmem, *otp_user_nvmem, *otp_factory_nvmem, *owner;
    int (*_get_device)(struct mtd_info *);
    int (*_read_oob)(struct mtd_info *, loff_t, struct mtd_oob_ops *);
    int (*_read)(struct mtd_info *, loff_t, size_t, size_t *, u8 *);
    void (*_put_device)(struct mtd_info *), (*_reboot)(struct mtd_info *);
    int reboot_notifier, index;
    const char *name;
    struct list_head partitions;
    struct { struct list_head node; u64 offset; } part;
    struct { struct lock partitions_lock; } master;
};
struct mtd_notifier { struct list_head list; void (*remove)(struct mtd_info *); };
struct nand_chip { struct mtd_info mtd; bool resources; };
struct sunxi_nand_chip { struct nand_chip nand; struct list_head node; };
struct sunxi_nfc { struct list_head chips; };
static struct lock mtd_table_mutex;
static struct mtd_info *mtd_idr[8];
#define mtd_for_each_device(p) for(unsigned id_=0;id_<8;id_++) if(((p)=mtd_idr[id_]))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
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
static void mtd_release(struct device *);
static void device_unregister(struct device *d)
{ assert(d->registered); d->registered=false; releases++; mtd_release(d); }
static struct mtd_info *dev_get_drvdata(struct device *d)
{ return container_of(d,struct mtd_info,dev); }
static void idr_remove(void *idr,int index)
{ assert(mtd_idr[index]); mtd_idr[index]=NULL; }
static void *mtd_get_of_node(struct mtd_info *m) { return NULL; }
static void of_node_put(void *n) { }
static void release_mtd_partition(struct mtd_info *m) { assert(m->parent); }
static int mtd_class;
#define MTD_DEVT(i) ((i)*2)
static void device_destroy(void *cls,dev_t dev) { }
static void unregister_reboot_notifier(void *n) { }
static int mtd_virt_concat_destroy(struct mtd_info *m) { assert(0); return 0; }
static bool try_module_get(void *p) { if (!module_available) return false; owner_refs++; return true; }
static void module_put(void *p) { if (p) { assert(owner_refs); owner_refs--; } else { assert(module_refs); module_refs--; } }
static void kref_get(struct kref *r) { assert(r->n); r->n++; }
static void kref_put(struct kref *r, void (*release)(struct kref *))
{ assert(r->n); if (!--r->n) release(r); }
static void nand_cleanup(struct nand_chip *c)
{ assert(c->resources); c->resources=false; cleanup_calls++; }
