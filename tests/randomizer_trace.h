/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Transport only records the control word and returns the injected status. */
static int transfer_result;
static u32 transfer_control;
static int sunxi_nfc_read_buf(struct nand_chip *n, u8 *buf, int len)
{
    transfer_control = readl(n->controller->regs + NFC_REG_ECC_CTL);
    return transfer_result;
}
static int sunxi_nfc_write_buf(struct nand_chip *n, const u8 *buf, int len)
{
    transfer_control = readl(n->controller->regs + NFC_REG_ECC_CTL);
    return transfer_result;
}
struct randomizer_input {
    unsigned hw, mode, step, ppb, page;
    bool bbm;
    int fail;
    u8 marker[4], decoded[4];
};
