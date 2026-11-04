/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Measured H616 NORMAL/block-mode semantics, not a replacement kernel body.
 * PAGE_OP/ECC_OP parity remains the existing opaque-codeword contract. */
void measured_options(bool dirty, unsigned fault)
{
    measured_dirty=false; measured_inject_dirty=dirty;
    measured_write_fault=fault; measured_confirms=0;
    media_wire_fault(0,0);
}

void measured_soft_reset(bool was_active)
{
    if (was_active && measured_inject_dirty) measured_dirty=true;
}

static void measured_normal(u8 *buf, unsigned len, bool write)
{
    /* Normal block reads/writes restart at the configured seed. A non-word
     * read leaves its last requested byte un-XORed. The register is not an
     * observable internal phase counter. */
    assert(!(readl(controller.regs+NFC_REG_ECC_CTL)&(BIT(7)|BIT(6))));
    unsigned transformed=write ? len : len-!!(len%4);
    transform(buf,transformed,transport_seed());
    if (!write && measured_dirty && column==0 && len==1024) {
        u8 original[1024]; memcpy(original,buf,sizeof(original));
        for (unsigned i=0;i<len;i++) buf[i]=original[(i+52)%len];
        measured_dirty=false;
    }
    request_event(write ? "normal-write" : "normal-read",column,len);
}

unsigned measured_confirm_count(void) { return measured_confirms; }

void measured_layout(unsigned *first,unsigned *second,unsigned *tail)
{
    *first=sunxi_nfc_user_data_sz(&persistent_chip,0);
    *second=sunxi_nfc_user_data_sz(&persistent_chip,1);
    *tail=sunxi_get_oob_offset(&persistent_chip,&persistent_chip.ecc,2);
}

void measured_store(FILE *output)
{
    assert(fwrite(flash[0][17],B_PAGE+B_SPARE,1,output)==1);
}

bool measured_erased(void)
{
    return !memchr_inv(flash[0][17],255,B_PAGE+B_SPARE);
}
