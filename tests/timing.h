/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Only API/structure fixtures; the solver and register packing are extracted. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
#define BIT(n) (UINT32_C(1) << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define NSEC_PER_SEC 1000000000UL
#define NAND_DATA_IFACE_CHECK_ONLY -1
#define ENOTSUPP 524
#define IS_ERR(p) ((intptr_t)(p) < 0 && (intptr_t)(p) >= -4095)
#define dev_err(...) ((void)0)

#define TIMING_FIELDS(X) \
    X(tCLS_min) X(tCLH_min) X(tCS_min) X(tCH_min) X(tWP_min) \
    X(tWH_min) X(tALS_min) X(tDS_min) X(tDH_min) X(tRR_min) \
    X(tALH_min) X(tRP_min) X(tREH_min) X(tRC_min) X(tWC_min) \
    X(tWB_max) X(tADL_min) X(tWHR_min) X(tRHW_min) X(tREA_max) X(tRLOH_min)
#define DECLARE_FIELD(f) u32 f;
struct nand_sdr_timings { TIMING_FIELDS(DECLARE_FIELD) };
#undef DECLARE_FIELD
struct nand_interface_config { struct nand_sdr_timings sdr; bool invalid; };
struct sunxi_nfc_timings;
struct sunxi_nfc_caps { const struct sunxi_nfc_timings *timings; };
struct sunxi_nfc { struct sunxi_nfc_caps *caps; void *mod_clk, *dev; };
struct nand_chip { struct sunxi_nfc *controller; };
struct sunxi_nand_chip {
    struct nand_chip nand;
    u32 timing_cfg, timing_ctl;
    unsigned long clk_rate;
};
#define to_sunxi_nand(n) ((struct sunxi_nand_chip *)(n))
#define to_sunxi_nfc(n) (n)
static const struct nand_sdr_timings *
nand_get_sdr_timings(const struct nand_interface_config *conf)
{
    return conf->invalid ? (const void *)(intptr_t)-EINVAL : &conf->sdr;
}
static long round_response;
static unsigned clock_calls;
static unsigned long clock_request;
static long clk_round_rate(void *clock, unsigned long requested)
{
    clock_calls++;
    clock_request = requested;
    return round_response;
}
struct timing_input {
    unsigned profile;
    u32 d[21];
    long rounded;
    bool check, invalid;
};
