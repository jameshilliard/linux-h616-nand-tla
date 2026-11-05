/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host contracts, not replacements for device core, ID parsers or ECC engines. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define struct_size(p,m,n) (sizeof(*(p))+sizeof((p)->m[0])*(n))
#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
static int warning(bool condition) { return condition; }
#define WARN_ON(e) warning(!!(e))
#define WARN(c,...) warning(!!(c))
#define pr_warn(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_err_probe(d,e,...) (e)
#define fallthrough __attribute__((fallthrough))
#define NAND_BBT_USE_FLASH 0x20000
#define NAND_BBT_NO_OOB 0x40000
#define NAND_BBT_NO_OOB_BBM 0x80000
#define NAND_BBT_DYNAMICSTRUCT 0x80000000
#define NAND_NO_SUBPAGE_WRITE BIT(9)
#define NAND_ROM BIT(11)
#define NAND_SUBPAGE_READ BIT(12)
#define NAND_SKIP_BBTSCAN BIT(16)
#define NAND_SCAN_SILENT_NODEV BIT(18)
#define NAND_NEED_SCRAMBLING BIT(13)
#define MTD_CAP_ROM 0
#define NFC_MAX_CS 7
#define NFC_REG_INT 8
#define ONFI_SUBFEATURE_PARAM_LEN 4
#define ONFI_FEATURE_ADDR_TIMING_MODE 1
#define ONFI_DATA_INTERFACE_SDR 0
#define ONFI_DATA_INTERFACE_NVDDR 0x10
#define ONFI_TIMING_MODE_PARAM(x) ((x)&15)
enum { NAND_ECC_ENGINE_TYPE_INVALID, NAND_ECC_ENGINE_TYPE_NONE,
       NAND_ECC_ENGINE_TYPE_SOFT, NAND_ECC_ENGINE_TYPE_ON_HOST, NAND_ECC_ENGINE_TYPE_ON_DIE };
enum { NAND_ECC_ALGO_HAMMING, NAND_ECC_ALGO_BCH };
typedef int (*io_fn)(void);
struct list_head { struct list_head *next, *prev; };
static void probe_trace_list(const char *, struct list_head *);
static void INIT_LIST_HEAD(struct list_head *h) { h->next=h->prev=h; }
static bool list_empty(struct list_head *h) { return h->next == h; }
static void list_add_tail(struct list_head *n, struct list_head *h)
{ n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n; probe_trace_list("link",n); }
static void list_del(struct list_head *n)
{ n->prev->next=n->next; n->next->prev=n->prev; probe_trace_list("unlink",n); }
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
#define list_for_each_entry(p,h,m) \
    for (p=container_of((h)->next,typeof(*(p)),m); &(p)->m!=(h); \
         p=container_of((p)->m.next,typeof(*(p)),m))
struct device_node {
    struct device_node *child, *next;
    int count;
    u32 cs[2], rb[2];
    bool vendor, absent;
    const char *status;
};
struct device { struct device_node *of_node; struct device *parent; };
struct platform_device { struct device dev; void *data; };
struct resource { unsigned long start; };
struct nand_chip;
struct nand_interface_config { struct { int mode; } timings; bool nvddr; };
struct nand_controller_ops {
    int (*attach_chip)(struct nand_chip *);
    void (*detach_chip)(struct nand_chip *);
    int (*setup_interface)(struct nand_chip *, int, const struct nand_interface_config *);
};
struct nand_controller { const struct nand_controller_ops *ops; int lock; };
struct nand_ecc_props { unsigned strength, step_size; };
struct nand_memory_organization { unsigned ntargets; };
struct nand_device {
    struct nand_memory_organization memorg;
    struct { struct { struct nand_ecc_props conf; unsigned nsteps,total; } ctx; } ecc;
    struct nand_ecc_props requirements;
    void *fixture_dev;
};
struct nand_ecc_ctrl {
    unsigned engine_type, algo, size, strength, bytes, steps, total;
    io_fn read_page, write_page, read_page_raw, write_page_raw, read_oob, write_oob,
          read_oob_raw, write_oob_raw, correct, calculate;
    void *code_buf, *calc_buf;
};
struct mtd_info {
    struct device dev;
    const char *name;
    unsigned writesize,oobsize,oobavail,erasesize,ecc_strength,ecc_step_size,
             subpage_sft,bitflip_threshold,flags;
    uint64_t size;
    void *owner;
    const void *ooblayout;
    io_fn _erase,_point,_unpoint,_panic_write,_read_oob,_write_oob,_sync,_lock,
          _unlock,_suspend,_resume,_reboot,_block_isreserved,_block_isbad,
          _block_markbad,_max_bad_blocks;
};
struct nand_chip {
    struct nand_controller *controller;
    struct nand_device base;
    struct mtd_info mtd;
    struct nand_ecc_ctrl ecc;
    struct device_node *flash_node;
    unsigned options,bbt_options,page_shift,subpagesize;
    int cur_cs,lock,resume_wq;
    struct { int page; } pagecache;
    struct { u8 data[2]; } id;
    struct { void *model,*onfi; bool supports_set_get_features;
             unsigned long get_feature_list[1],set_feature_list[1]; } parameters;
    const struct nand_interface_config *current_interface_config;
    struct nand_interface_config *best_interface_config;
    u8 *data_buf,*oob_poi;
    void *secure_regions,*bbt,*fixture_manuf;
    struct { unsigned options; } *badblock_pattern;
    bool fixture_registered;
    bool suspended;
};
struct nand_flash_dev { int unused; };
struct sunxi_nand_chip_sel { u8 cs; int rb; };
struct sunxi_nfc_oob_ops { int unused; };
struct sunxi_nand_chip {
    struct list_head node;
    struct nand_chip nand;
    const struct sunxi_nfc_oob_ops *oob_ops;
    u8 *user_data_bytes;
    bool registered;
    int nsels;
    struct sunxi_nand_chip_sel sels[];
};
struct sunxi_nfc_caps { bool has_ecc_clk,has_mbus_clk,needs_bus_reset; };
struct sunxi_nfc {
    struct device *dev;
    struct nand_controller controller;
    struct list_head chips;
    void *regs,*ahb_clk,*mod_clk,*ecc_clk,*mbus_clk,*reset,*dmac;
    unsigned long assigned_cs;
    int complete,irq;
    const struct sunxi_nfc_caps *caps;
};
/* @prototypes@ */
static unsigned assertions, cases_run, events, fail_at, nalloc, registered, dma_live, clock_live;
static unsigned mode_setup_calls, set_calls, get_calls, reset_calls;
static int host_mode, chip_mode[2], timing_fault, extra_die_fault;
static bool have_caps, external_dma, with_calc, slc, no_features, nvddr;
static bool hold_first_user;
static struct nand_chip *held_user;
static unsigned detects, unavailable_detects, scan_with_busy_controller;
static unsigned scanned, expected_scans, register_calls, unregister_calls;
static unsigned options, engine, mode, writesize, oobsize, req_step, req_strength;
static struct { void *p; bool managed,live; } allocations[256];
static const char *event_names[256];
#define CHECK(e) do { assertions++; if (!(e)) { fprintf(stderr,"FAIL %s:%d: %s (event %u / fail %u)\n",__FILE__,__LINE__,#e,events,fail_at); exit(1); } } while (0)
/* Observers run only in the explicit trace matrix. No extracted kernel body
 * is edited. Snapshot membership is read from the actual fixture list and
 * MTD registration state, independently of the TLA transition function. */
static FILE *probe_trace_file;
static struct device_node *probe_trace_nodes;
static struct nand_chip *probe_trace_chips[3];
static struct sunxi_nfc *probe_trace_nfc;
static unsigned probe_trace_count, probe_trace_events;
static unsigned probe_trace_id(struct nand_chip *c)
{
    for (unsigned i=0;i<probe_trace_count;i++)
        if (c->flash_node==&probe_trace_nodes[i]) return i+1;
    CHECK(!"unknown chip in probe trace"); return 0;
}
static void probe_trace_event(const char *kind, struct nand_chip *c)
{
    if (!probe_trace_file) return;
    unsigned id=c?probe_trace_id(c):0;
    if (c) { probe_trace_chips[id-1]=c; probe_trace_nfc=to_sunxi_nfc(c->controller); }
    fprintf(probe_trace_file,"%s{\"kind\":\"%s\",\"chip\":%u,\"listed\":[",
            probe_trace_events++?",":"",kind,id);
    unsigned count=0;
    if (probe_trace_nfc) {
        struct sunxi_nand_chip *s;
        list_for_each_entry(s,&probe_trace_nfc->chips,node)
            fprintf(probe_trace_file,"%s%u",count++?",":"",probe_trace_id(&s->nand));
    }
    fprintf(probe_trace_file,"],\"published\":["); count=0;
    for (unsigned i=0;i<probe_trace_count;i++)
        if (probe_trace_chips[i] && probe_trace_chips[i]->fixture_registered)
            fprintf(probe_trace_file,"%s%u",count++?",":"",i+1);
    fprintf(probe_trace_file,"]}");
}
static void probe_trace_list(const char *kind, struct list_head *node)
{
    if (probe_trace_file)
        probe_trace_event(kind,&container_of(node,struct sunxi_nand_chip,node)->nand);
}
static bool fail(const char *name) { events++; CHECK(events<256); event_names[events]=name; return events == fail_at; }
static void *allocate(size_t n, bool managed)
{
    if (fail(managed ? "devm_alloc" : "alloc")) return NULL;
    CHECK(nalloc<256); void *p=calloc(1,n ? n : 1); CHECK(p);
    allocations[nalloc++]=(typeof(allocations[0])){p,managed,true}; return p;
}
static void release(const void *p)
{
    if (!p) return;
    for (unsigned i=nalloc;i;i--) if (allocations[i-1].p==p && allocations[i-1].live) {
        allocations[i-1].live=false; free((void *)p); return;
    }
    CHECK(!"double free or unowned allocation");
}
#define kmalloc(n,f) allocate(n,false)
#define kfree(p) release(p)
#define devm_kzalloc(d,n,f) allocate(n,true)
#define devm_kfree(d,p) ((void)(d), release(p))
static struct mtd_info *nand_to_mtd(struct nand_chip *c) { return &c->mtd; }
static struct nand_chip *mtd_to_nand(struct mtd_info *m) { return container_of(m,struct nand_chip,mtd); }
static const struct nand_ecc_props *nanddev_get_ecc_requirements(struct nand_device *d) { return &d->requirements; }
static struct nand_memory_organization *nanddev_get_memorg(struct nand_device *d) { return &d->memorg; }
static unsigned nanddev_ntargets(struct nand_device *d) { return d->memorg.ntargets; }
static uint64_t nanddev_target_size(struct nand_device *d) { return 1<<20; }
static bool nand_is_slc(struct nand_chip *c) { return slc; }
static bool nand_ecc_is_strong_enough(struct nand_device *d) { return true; }
static const char *dev_name(struct device *d) { return "fixture-nand"; }
static void mutex_init(int *p) { *p=0; }
static void mutex_lock(int *p) { CHECK(!*p); *p=1; }
static void mutex_unlock(int *p) { CHECK(*p); *p=0; }
#define wait_event(q,condition) CHECK(condition)
static void init_waitqueue_head(int *p) { *p=1; }
static void init_completion(int *p) { *p=0; }
static void nand_controller_init(struct nand_controller *c) { mutex_init(&c->lock); }
static bool test_bit(unsigned b,const unsigned long *p) { return !!(*p&BIT(b)); }
static bool test_and_set_bit(unsigned b,unsigned long *p) { bool old=test_bit(b,p); *p|=BIT(b); return old; }
static void nand_set_flash_node(struct nand_chip *c,struct device_node *n) { c->flash_node=n; }
static struct device_node *nand_get_flash_node(struct nand_chip *c) { return c->flash_node; }
static bool of_property_read_bool(struct device_node *n,const char *s) { return n->vendor; }
static const void *of_get_property(struct device_node *n,const char *s,int *len)
{ *len=n->count*4; return n->count<0 ? NULL : n->cs; }
static int of_property_read_u32_index(struct device_node *n,const char *s,int i,u32 *v)
{ if (fail("dt-index")) return -EINVAL; *v=!strcmp(s,"reg")?n->cs[i]:n->rb[i]; return 0; }
/* No OF references are allocated: iterator semantics are an explicit fixture boundary. */
#define for_each_child_of_node_scoped(n,c) for (struct device_node *c=(n)->child;c;c=c->next)
static bool of_device_is_available(struct device_node *n)
{ return !n->status || !strcmp(n->status,"okay") || !strcmp(n->status,"ok"); }
static struct device_node *next_available(struct device_node *n)
{ while (n && !of_device_is_available(n)) n=n->next; return n; }
#define for_each_available_child_of_node_scoped(n,c) \
    for (struct device_node *c=next_available((n)->child);c;c=next_available(c->next))
static int io_stub(void) { CHECK(!"I/O callback unexpectedly executed during scan"); return 0; }
#define nand_read_oob_std io_stub
#define nand_write_oob_std io_stub
#define nand_read_page_raw io_stub
#define nand_write_page_raw io_stub
#define nand_erase io_stub
#define panic_nand_write io_stub
#define nand_read_oob io_stub
#define nand_write_oob io_stub
#define nand_sync io_stub
#define nand_lock io_stub
#define nand_unlock io_stub
#define nand_suspend io_stub
#define nand_resume io_stub
#define nand_shutdown io_stub
#define nand_block_isreserved io_stub
#define nand_block_isbad io_stub
#define nand_block_markbad io_stub
#define nanddev_mtd_max_bad_blocks io_stub
static const int rawnand_ops, layout;
static const struct sunxi_nfc_oob_ops sunxi_nfc_plain_oob_ops __attribute__((unused)),
    sunxi_nfc_scrambled_plain_bbm_oob_ops __attribute__((unused));
static const void *nand_get_small_page_ooblayout(void) { return &layout; }
static const void *nand_get_large_page_hamming_ooblayout(void) { return &layout; }
static const void *nand_get_large_page_ooblayout(void) { return &layout; }
static void mtd_set_ooblayout(struct mtd_info *m,const void *p) { m->ooblayout=p; }
static int mtd_ooblayout_count_freebytes(struct mtd_info *m) { return 6; }
static const struct nand_interface_config reset_config;
static const struct nand_interface_config *nand_get_reset_interface_config(void) { return &reset_config; }
static bool nand_controller_can_setup_interface(struct nand_chip *c) { return c->controller->ops->setup_interface!=NULL; }
static bool nand_interface_is_sdr(const struct nand_interface_config *p) { return !p->nvddr; }
static bool nand_interface_is_nvddr(const struct nand_interface_config *p) { return p->nvddr; }
static void nand_select_target(struct nand_chip *c,int n) { c->cur_cs=n; }
static void nand_deselect_target(struct nand_chip *c) { c->cur_cs=-1; }
static int setup_interface(struct nand_chip *c,int n,const struct nand_interface_config *p)
{
    mode_setup_calls++;
    if ((timing_fault==2 && p->timings.mode) || (timing_fault==5 && !p->timings.mode) || fail("setup-mode")) return -EIO;
    host_mode=p->timings.mode; return 0;
}
static int nand_set_features(struct nand_chip *c,int a,u8 *p)
{ set_calls++; if (timing_fault==1 || fail("set-feature")) return -EIO; chip_mode[c->cur_cs]=p[0]; return 0; }
static int nand_get_features(struct nand_chip *c,int a,u8 *p)
{ get_calls++; if (timing_fault==3 || fail("get-feature")) return -EIO; p[0]=(timing_fault>=4)?0:chip_mode[c->cur_cs]; return 0; }
static int nand_reset_op(struct nand_chip *c)
{ reset_calls++; if (timing_fault==6 || fail("reset-op")) return -EIO; chip_mode[c->cur_cs]=0; return 0; }
static int nand_reset(struct nand_chip *c,int n) { return extra_die_fault==1 ? -EIO : 0; }
static int nand_readid_op(struct nand_chip *c,int a,u8 *p,unsigned n)
{ p[0]=extra_die_fault==3?0:0x2c; p[1]=0xda; return extra_die_fault==2?-EIO:0; }
static int rawnand_dt_init(struct nand_chip *c)
{
    if (fail("dt-init")) return -EINVAL;
    c->options=options; c->ecc.engine_type=engine; c->ecc.algo=NAND_ECC_ALGO_HAMMING;
    c->base.requirements=(struct nand_ecc_props){req_strength,req_step}; return 0;
}
static void nand_set_defaults(struct nand_chip *c) { }
static int nand_legacy_check_hooks(struct nand_chip *c) { return fail("legacy-hooks")?-EINVAL:0; }
/* Geometry/ID decoding is deliberately a separate boundary, not claimed covered. */
static int nand_detect(struct nand_chip *c,struct nand_flash_dev *t)
{
    detects++;
    if (!of_device_is_available(c->flash_node)) unavailable_detects++;
    /* A schedule boundary, not an emulation of ID parsing. The real detector
     * calls nand_reset()/nand_readid_op() here without controller locking. */
    if (held_user && held_user != c) {
        CHECK(held_user->fixture_registered);
        CHECK(held_user->controller==c->controller && c->controller->lock);
        scan_with_busy_controller++;
    }
    if (c->flash_node->absent) return -ENODEV;
    if (fail("detect")) return -ENODEV;
    c->parameters.model=allocate(16,false); if (!c->parameters.model) return -ENOMEM;
    c->id.data[0]=0x2c; c->id.data[1]=0xda;
    c->mtd.writesize=writesize; c->mtd.oobsize=oobsize; c->mtd.erasesize=writesize*64;
    c->parameters.supports_set_get_features=!no_features;
    c->parameters.get_feature_list[0]=c->parameters.set_feature_list[0]=BIT(1);
    c->page_shift=11; return 0;
}
static int nand_manufacturer_init(struct nand_chip *c)
{ c->fixture_manuf=allocate(4,false); return c->fixture_manuf?0:-ENOMEM; }
static void nand_manufacturer_cleanup(struct nand_chip *c)
{ release(c->fixture_manuf); c->fixture_manuf=NULL; }
static int nand_set_ecc_on_host_ops(struct nand_chip *c) { return fail("ecc-ops")?-EINVAL:0; }
static int nand_set_ecc_soft_ops(struct nand_chip *c)
{ c->ecc.size=256; c->ecc.strength=1; c->ecc.bytes=3; c->ecc.correct=io_stub; c->mtd.ooblayout=&layout; return fail("soft-ops")?-EINVAL:0; }
static void rawnand_sw_hamming_cleanup(struct nand_chip *c) { }
static void rawnand_sw_bch_cleanup(struct nand_chip *c) { }
static int nanddev_init(struct nand_device *d,const void *o,void *owner)
{ d->fixture_dev=allocate(4,false); return d->fixture_dev?0:-ENOMEM; }
static void nanddev_cleanup(struct nand_device *d) { release(d->fixture_dev); d->fixture_dev=NULL; }
static int nand_choose_interface_config(struct nand_chip *c)
{
    c->best_interface_config=allocate(sizeof(*c->best_interface_config),false);
    if (!c->best_interface_config) return -ENOMEM;
    *c->best_interface_config=(struct nand_interface_config){.timings.mode=mode,.nvddr=nvddr}; return 0;
}
static void rawnand_late_check_supported_ops(struct nand_chip *c) { }
static int of_get_nand_secure_regions(struct nand_chip *c)
{ c->secure_regions=allocate(4,false); return c->secure_regions?0:-ENOMEM; }
/* Actual BBT paths (including failures) are executed by run_core.py. */
static int nand_create_bbt(struct nand_chip *c)
{ c->bbt=allocate(8,false); return c->bbt?0:-ENOMEM; }
/* Real ECC geometry/attachment is executed by run_c.py; this fixture owns its resources. */
static int sunxi_nand_hw_ecc_ctrl_init(struct nand_chip *c,struct nand_ecc_ctrl *e,struct device_node *n)
{
    struct sunxi_nand_chip *s=to_sunxi_nand(c);
    s->user_data_bytes=allocate(writesize/e->size,true); if (!s->user_data_bytes) return -ENOMEM;
    e->bytes=28; e->read_page=e->write_page=e->read_oob=e->write_oob=io_stub;
    if (with_calc) e->calculate=io_stub;
    c->mtd.ooblayout=&layout; return fail("attach-after-alloc")?-EINVAL:0;
}
static int sunxi_nfc_allwinner_oob_attach_chip(struct nand_chip *c,struct device_node *n)
{ if (c->ecc.engine_type!=NAND_ECC_ENGINE_TYPE_ON_HOST) return -EINVAL;
  c->options|=NAND_NEED_SCRAMBLING|NAND_NO_SUBPAGE_WRITE;
  return sunxi_nand_hw_ecc_ctrl_init(c,&c->ecc,n); }
static const struct nand_controller_ops sunxi_nand_controller_ops={
    .attach_chip=sunxi_nand_attach_chip,.detach_chip=sunxi_nand_detach_chip,.setup_interface=setup_interface};
static int nand_scan(struct nand_chip *c,unsigned n)
{
    probe_trace_event("scan-start",c);
    int ret=nand_scan_with_ids(c,n,NULL);
    if (!ret) scanned++;
    probe_trace_event(ret?"scan-error":"scan-ok",c);
    return ret;
}
static int mtd_device_register(struct mtd_info *m,const void *p,int n)
{
    register_calls++;
    if (DEFERRED_PUBLICATION) CHECK(scanned==expected_scans);
    if (fail("register")) { probe_trace_event("register-error",mtd_to_nand(m)); return -EIO; }
    CHECK(!mtd_to_nand(m)->fixture_registered);
    mtd_to_nand(m)->fixture_registered=true; registered++;
    probe_trace_event("register-ok",mtd_to_nand(m));
    /* Model a user acquiring the first visible MTD after registration. This
     * executes the real NAND-core chip/controller lock acquisition bodies. */
    if (hold_first_user && !held_user) {
        held_user=mtd_to_nand(m);
        nand_get_device(held_user);
    }
    return 0;
}
static int mtd_device_unregister(struct mtd_info *m)
{
    CHECK(mtd_to_nand(m)->fixture_registered && registered);
    mtd_to_nand(m)->fixture_registered=false;
    registered--; unregister_calls++;
    probe_trace_event("unregister",mtd_to_nand(m));
    return 0;
}
static struct resource resource;
static struct sunxi_nfc_caps caps={.has_ecc_clk=true,.has_mbus_clk=true};
static unsigned regs[16];
static void *devm_platform_get_and_ioremap_resource(struct platform_device *p,int i,struct resource **r)
{ *r=&resource; return fail("ioremap")?ERR_PTR(-ENOMEM):regs; }
static int platform_get_irq(struct platform_device *p,int n) { return fail("irq-number")?-EINVAL:1; }
static const void *of_device_get_match_data(struct device *d) { return have_caps?&caps:NULL; }
static void *devm_clk_get_enabled(struct device *d,const char *s)
{ if (fail(s)) return ERR_PTR(-EIO); clock_live++; return (void *)1; }
static void *devm_reset_control_get_optional_exclusive(struct device *d,const char *s)
{ return fail("reset-get")?ERR_PTR(-EIO):(void *)1; }
static void *devm_reset_control_get_exclusive(struct device *d,const char *s)
{ return fail("mandatory-reset-get")?ERR_PTR(-ENOENT):(void *)1; }
static bool reset_deasserted;
static unsigned reset_pulses, reset_deasserts;
static int reset_control_deassert(void *r)
{ reset_deasserts++; if (fail("reset-deassert")) return -EIO; reset_deasserted=true; return 0; }
static int reset_control_reset(void *r)
{
    CHECK(r && caps.needs_bus_reset); reset_pulses++;
    if (fail("reset-pulse")) return -EIO;
    reset_deasserted=true; return 0;
}
static int reset_control_assert(void *r)
{
    if (caps.needs_bus_reset && !reset_deasserted && fail("initial-reset-assert"))
        return -EIO;
    reset_deasserted=false; return 0;
}
static void udelay(unsigned n) { CHECK(n==2); }
static int sunxi_nfc_rst(struct sunxi_nfc *n) { return fail("controller-reset")?-ETIMEDOUT:0; }
static void writel(unsigned v,void *p) { *(unsigned *)p=v; }
static int sunxi_nfc_interrupt(int irq,void *p) { return 0; }
static int devm_request_irq(struct device *d,int i,int (*f)(int,void *),int a,const char *s,void *p)
{ return fail("request-irq")?-EBUSY:0; }
static int sunxi_nfc_dma_init(struct sunxi_nfc *n,struct resource *r)
{ if (fail("dma-init")) return -EPROBE_DEFER; if (external_dma) { n->dmac=(void *)1; dma_live++; } return 0; }
static void dma_release_channel(void *p) { CHECK(dma_live); dma_live--; }
/* Allocation/free and failed-engine retention are exercised with the exact
 * helper in run_lowlevel.py; lifecycle tests use external DMA only. */
static void sunxi_nfc_dma_release(struct sunxi_nfc *n)
{ if(n->dmac) dma_release_channel(n->dmac); }
static void platform_set_drvdata(struct platform_device *p,void *d) { p->data=d; }
static void *platform_get_drvdata(struct platform_device *p) { return p->data; }
