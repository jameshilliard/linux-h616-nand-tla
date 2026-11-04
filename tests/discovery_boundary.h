/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
typedef uint8_t u8;
typedef uint16_t u16, __le16;
typedef uint32_t __le32;
#define __packed __attribute__((packed))
#define BIT(n) (1U<<(n))
#define le16_to_cpu(v) (v)
#define le32_to_cpu(v) (v)
#define GFP_KERNEL 0
#define ONFI_PARAM_PAGES 3
#define NAND_BUSWIDTH_16 2
#define NAND_CI_CELLTYPE_MSK 0x0c
#define NAND_CI_CELLTYPE_SHIFT 2
#define pr_debug(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define dev_dbg(d,...) ((void)(d))
/* @onfi@ */
struct nand_ecc_props { unsigned strength,step_size; };
struct nand_memory_organization {
    unsigned pagesize,oobsize,pages_per_eraseblock,luns_per_target,planes_per_lun;
    unsigned eraseblocks_per_lun,max_bad_eraseblocks_per_lun,bits_per_cell;
};
struct nand_device { struct nand_memory_organization mem; struct nand_ecc_props req; };
struct mtd_info { unsigned writesize,oobsize,erasesize; int dev; };
struct nand_controller { struct { bool data_only_read; } supported_op; };
struct nand_chip;
struct manuf_ops { void (*fixup_onfi_param_page)(struct nand_chip *,struct nand_onfi_params *); };
struct manuf_desc { struct manuf_ops *ops; };
struct nand_chip {
    struct nand_device base; struct mtd_info mtd; unsigned options;
    struct nand_controller *controller;
    struct { struct manuf_desc *desc; } manufacturer;
    struct { char *model; struct onfi_params *onfi; bool supports_set_get_features,supports_read_cache;
        unsigned get_feature_list[8],set_feature_list[8]; } parameters;
    struct { u8 data[8]; } id;
};
struct nand_flash_dev { unsigned pagesize,erasesize; };
#define nand_to_mtd(c) (&(c)->mtd)
#define nanddev_get_memorg(b) (&(b)->mem)
static void nanddev_set_ecc_requirements(struct nand_device *b,const struct nand_ecc_props *r) { b->req=*r; }
static unsigned fls(unsigned v) { return v?32-__builtin_clz(v):0; }
static unsigned allocations,fail_alloc;
static void *alloc(size_t n) { return ++allocations==fail_alloc?NULL:calloc(1,n); }
#define kzalloc(n,f) alloc(n)
#define kmalloc(n,f) alloc(n)
#define kzalloc_obj(o) alloc(sizeof(o))
#define kfree(p) free(p)
static char *kstrdup(const char *s,int f) { char *p=alloc(strlen(s)+1); if(p) strcpy(p,s); return p; }
static char *strim(char *s) { size_t n=strlen(s); while(n && isspace(s[n-1])) s[--n]=0; while(isspace(*s)) s++; return s; }
static void bitmap_set(unsigned *a,unsigned bit,unsigned n) { assert(n==1); a[bit/32]|=BIT(bit%32); }
static struct nand_onfi_params copies[3];
static u8 extended[64];
static unsigned transport_count,fail_transport,next_copy;
static bool has_exec=true,has_signature=true;
static bool nand_has_exec_op(struct nand_chip *c) { return has_exec; }
static void nand_legacy_adjust_cmdfunc(struct nand_chip *c) { }
static int nand_readid_op(struct nand_chip *c,int addr,void *buf,size_t len)
{ assert(addr==0x20 && len==4); memcpy(buf,has_signature?"ONFI":"none",len); return 0; }
static int read_copy(unsigned i,void *b,size_t n)
{ assert(i<3 && n==sizeof(copies[0])); if (++transport_count==fail_transport) return -EIO; memcpy(b,&copies[i],n); next_copy=i+1; return 0; }
static int nand_read_param_page_op(struct nand_chip *c,int addr,void *b,size_t n) { return read_copy(0,b,n); }
static int nand_read_data_op(struct nand_chip *c,void *b,size_t n,bool a,bool z) { return read_copy(next_copy,b,n); }
static int nand_change_read_column_op(struct nand_chip *c,unsigned col,void *b,size_t n,bool force)
{
    if(col<3*sizeof(copies[0])) return read_copy(col/sizeof(copies[0]),b,n);
    assert(col==3*sizeof(copies[0]) && n<=sizeof(extended));
    if (++transport_count==fail_transport) return -EIO;
    memcpy(b,extended,n); return 0;
}
extern int driver_attach_check(unsigned,unsigned,unsigned,unsigned,bool,bool,bool);
