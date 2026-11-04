/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "transport_api.h"
#define likely(x) (x)
#define kcalloc(n, size, flags) calloc(n, size)
#define kfree(p) free(p)
#define NAND_COMMON_TIMING_NS(conf, name) ((void)(conf), 1)
#define NAND_COMMON_TIMING_MS(conf, name) ((void)(conf), 1)
#define nanddev_ntargets(base) ((base)->ntargets)
static const struct nand_interface_config interface;
static const struct nand_interface_config *nand_get_interface_config(struct nand_chip *n)
{ return &interface; }
static bool nand_interface_is_nvddr(const struct nand_interface_config *c) { return false; }
static bool rawnand_cont_read_ongoing(struct nand_chip *n, unsigned page) { return false; }
static int nand_lp_exec_cont_read_page_op(struct nand_chip *n, unsigned page,
        unsigned col, void *b, unsigned len, bool check)
{ assert(!"continuous reads outside this SDR fixture"); return -ENOTSUPP; }
static bool nand_supports_set_features(struct nand_chip *n, int addr)
{ return addr == ONFI_FEATURE_ADDR_READ_RETRY; }
static void *nand_get_manufacturer_data(struct nand_chip *n) { return n->manufacturer; }
struct hynix_read_retry { int nregs; const u8 *regs; u8 values[]; };
struct hynix_nand { const struct hynix_read_retry *read_retry; };
static int sunxi_nfc_exec_op(struct nand_chip *, const struct nand_operation *, bool);
