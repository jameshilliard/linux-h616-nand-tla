/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <sys/types.h>
struct kref { unsigned n; };
struct module { unsigned refs; bool live; };
static struct module core_module={.live=true}, driver_module={.live=true};
#define THIS_MODULE (&core_module)
struct completion { bool done; };
#define DECLARE_COMPLETION_ONSTACK(n) struct completion n={0}
static void complete(struct completion *c) { c->done=true; }
static void drain_devices(void);
static void wait_for_completion(struct completion *c) { drain_devices(); assert(c->done); }
struct device_node { unsigned refs; };
struct device { bool registered; unsigned refs; void *type, *data; };
struct mtd_info {
    struct mtd_info *parent, *ref_master;
    struct kref refcnt;
    struct device dev;
    struct module *owner;
    bool registered;
    unsigned type, flags;
    int index;
    const char *name;
    struct device_node *np;
    struct { void *dfs_dir; } dbg;
    void *nvmem, *otp_user_nvmem, *otp_factory_nvmem;
    struct { int notifier_call; } reboot_notifier;
    int (*_get_device)(struct mtd_info *);
    void (*_put_device)(struct mtd_info *), (*_reboot)(struct mtd_info *), (*_sync)(struct mtd_info *);
    struct list_head partitions;
    struct { struct list_head node; } part;
    struct {
        struct lock partitions_lock;
        struct kref life_refcnt;
        void (*release)(struct mtd_info *);
        struct module *release_owner;
        struct completion *release_done;
    } master;
};
struct nand_controller { struct lock lock; };
struct nand_chip {
    struct mtd_info mtd;
    struct nand_controller *controller;
    struct lock lock;
    bool suspended, disconnected;
    int resume_wq;
    struct { int (*suspend)(struct nand_chip *); void (*resume)(struct nand_chip *); } ops;
};
struct sunxi_nand_chip { struct nand_chip nand; struct list_head node; };
struct sunxi_nfc { struct nand_controller controller; struct kref refcnt; struct device *dev; struct list_head chips; };
#define mtd_to_nand(m) container_of(m,struct nand_chip,mtd)
#define nand_to_mtd(c) (&(c)->mtd)
#define to_sunxi_nand(c) container_of(c,struct sunxi_nand_chip,nand)
#define to_sunxi_nfc(c) container_of(c,struct sunxi_nfc,controller)
#define nand_get_flash_node(c) ((c)->mtd.np)
#define mtd_is_partition(m) ((m)->parent!=NULL)
#define mtd_has_partitions(m) (!list_empty(&(m)->partitions))
#define mtd_get_of_node(m) ((m)->np)
#define CONFIG_MTD_VIRT_CONCAT 0
#define IS_REACHABLE(x) (x)
#define WARN_ON(x) assert(!(x))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define pr_debug(...) ((void)0)
static void (*suspend_wait_hook)(void);
#define wait_event(q,condition) do { if (!(condition)) { assert(suspend_wait_hook); suspend_wait_hook(); } assert(condition); } while (0)
static void wake_up_all(int *q) { (*q)++; }
static void lock_cleanup(struct lock **p) { if (*p) mutex_unlock(*p); }
#define guard(x) struct lock *guard_lock __attribute__((cleanup(lock_cleanup))) = take_lock
#define scoped_guard(x,p) for(struct lock *guard_lock __attribute__((cleanup(lock_cleanup)))=take_lock(p);guard_lock;mutex_unlock(guard_lock),guard_lock=NULL)
static struct lock *take_lock(struct lock *l) { mutex_lock(l); return l; }
static struct lock mtd_table_mutex;
static struct mtd_info *mtd_idr[16];
static bool mtd_ids[16];
static void ida_free(void *ida, unsigned id)
{ assert(mtd_ids[id]); mtd_ids[id]=false; }
static void *idr_find(void *id, int index) { return mtd_idr[index]; }
static void *idr_get_next(void *id, int *index)
{ for(;*index<16;(*index)++) if(mtd_idr[*index]) return mtd_idr[*index]; return NULL; }
static void idr_remove(void *id, int index) { assert(mtd_idr[index]); mtd_idr[index]=NULL; }
#define mtd_for_each_device(m) for(int index_=0; ((m)=__mtd_next_device(index_)); index_=(m)->index+1)
struct mtd_notifier { struct list_head list; void (*remove)(struct mtd_info *); };
static LIST_HEAD(mtd_notifiers);
static int mtd_class;
#define MTD_DEVT(i) ((i)*2)
static void device_destroy(void *cls, dev_t dev) { }
static void debugfs_remove_recursive(void *p) { }
static void nvmem_unregister(void *p) { }
static void unregister_reboot_notifier(void *p) { }
static int mtd_virt_concat_destroy(struct mtd_info *m) { assert(0); return 0; }
static bool device_is_registered(struct device *d) { return d->registered; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static void of_node_put(struct device_node *n) { if(n) { assert(n->refs); n->refs--; } }
static bool try_module_get(struct module *m) { if(!m) return true; if(!m->live) return false; m->refs++; return true; }
static void module_put(struct module *m) { if(m) { assert(m->refs); m->refs--; } }
static void kref_init(struct kref *r) { r->n=1; }
static void kref_get(struct kref *r) { assert(r->n); r->n++; }
static void kref_put(struct kref *r, void (*release)(struct kref *)) { assert(r->n); if(!--r->n) release(r); }
static unsigned cleaned, part_freed;
static void nand_cleanup(struct nand_chip *c)
{ assert(c->disconnected && !c->lock.held && !c->controller->lock.held); cleaned++; }
static void release_mtd_partition(struct mtd_info *m)
{ assert(m->parent && list_empty(&m->part.node)); free((void *)m->name); free(m); part_freed++; }
static struct device *pending[32];
static unsigned npending;
static bool delay_release;
static void mtd_release(struct device *);
static void put_device(struct device *d)
{ assert(d->refs); if(!--d->refs && d->type) { if(delay_release) pending[npending++]=d; else mtd_release(d); } }
static void device_unregister(struct device *d)
{ assert(d->registered); d->registered=false; put_device(d); }
static void drain_devices(void)
{ while(npending) mtd_release(pending[--npending]); }
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>=-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define FMODE_WRITE 2
#define MTD_ABSENT 0
#define MTD_WRITEABLE 0x400
struct inode { int minor; };
struct file { unsigned f_mode; void *private_data; };
struct mtd_file_info { struct mtd_info *mtd; int mode; };
#define iminor(i) ((i)->minor)
static bool fd_alloc_fail;
#define kzalloc_obj(x) (fd_alloc_fail ? NULL : calloc(1,sizeof(x)))
#define kfree(p) free((void *)(p))
