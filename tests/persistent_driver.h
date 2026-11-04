/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Command-level NAND/MMIO model: no analog BCH correction or real DMA bus. */
#define NFC_REG_ECC_CTL 0x34
#define NFC_RANDOM_SEED_MSK GENMASK(30,16)
#define NFC_RANDOM_SEED(s) ((u32)(s)<<16)
#define NFC_RANDOM_EN(n) ((n)->caps->random_en_mask)
enum { B_PAGE=2048, B_SPARE=256, B_PAGES=256, B_TARGETS=2 };
static u8 flash[B_TARGETS][B_PAGES][B_PAGE+B_SPARE], staging[B_PAGE+B_SPARE];
static unsigned row,target,dma_steps,dma_size;
static void *dma_buf;
static int fail_page=-1, ecc_error_page=-1;
static struct caps persistent_caps;
static struct nand_chip persistent_chip;
static bool program_pending;
static unsigned command_count, operation_count;
static int sunxi_nfc_write_buf(struct nand_chip *,const u8 *,int);
