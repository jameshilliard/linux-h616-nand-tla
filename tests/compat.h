/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Only the BSP's normal-page spare packing/length/seed operations are modeled. */
typedef int32_t s32;
#define MAX_ECC_BLK_CNT 32
#define MAX_UDATA_LEN_FOR_ECCBLOCK 32
#define UDATA_LEN_FOR_4BYTESPER1K 4
#define ERR_NO_28 (-28)
#define ERR_NO_29 (-29)
#define RAWNAND_ERR(...) ((void)0)
struct nand_controller_info {
    struct { u32 *reg_user_data_base, *reg_user_data_len_base; } nreg;
    struct { u8 udata_len_mode[32]; } nctri_cmd_seq;
};
struct nand_chip_info { unsigned ecc_sector, sector_cnt_per_page, sdata_bytes_per_page; };
static u32 bsp_readl(const void *p) { u32 value; memcpy(&value,p,4); return value; }
static void bsp_writel(u32 value,void *p) { memcpy(p,&value,4); }
