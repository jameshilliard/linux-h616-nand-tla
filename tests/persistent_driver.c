/* SPDX-License-Identifier: GPL-2.0-or-later */
static u16 transport_seed(void)
{ return (readl(controller.regs+NFC_REG_ECC_CTL)&NFC_RANDOM_SEED_MSK)>>16; }
static bool random_on(void)
{ return readl(controller.regs+NFC_REG_ECC_CTL)&NFC_RANDOM_EN(&controller); }
static void advance_seed(unsigned len)
{
    if (random_on()) {
        u32 ctl=readl(controller.regs+NFC_REG_ECC_CTL);
        u32 next=(ctl&~NFC_RANDOM_SEED_MSK)|NFC_RANDOM_SEED(sunxi_nfc_randomizer_step(transport_seed(),len*8+15));
        memcpy(controller.regs+NFC_REG_ECC_CTL,&next,4);
    }
}
static void transform(u8 *b,unsigned len,u16 seed)
{ if(random_on()) sunxi_nfc_randomize_buf(sunxi_nfc_randomizer_step(seed,15),b,len); }
static int transfer(struct nand_chip *n,unsigned col,void *dst,unsigned len)
{
    assert(col+len<=B_PAGE+B_SPARE && row<B_PAGES && target<B_TARGETS);
    if(flow_event(FP_READ)) return -ETIMEDOUT;
    if (len) {
        u8 *b=dst?dst:controller.regs+NFC_RAM0_BASE;
        assert(dst || len<=1024);
        memcpy(b,flash[target][row]+col,len); transform(b,len,transport_seed());
        advance_seed(len);
    }
    column=col+len; return 0;
}
static int sunxi_nfc_select_chip(struct nand_chip *n,unsigned cs)
{
    if (select_error) return select_error;
    assert(cs<B_TARGETS); target=cs;
    u32 base=n->mtd.writesize;
    memcpy(controller.regs+NFC_REG_SPARE_AREA(&controller),&base,4);
    return 0;
}
static int nand_read_page_op(struct nand_chip *n,unsigned page,unsigned col,void *dst,unsigned len)
{
    if((int)page==fail_page) return -EIO;
    row=page; sunxi_nfc_select_chip(n,n->cur_cs);
    return transfer(n,col,dst,len);
}
static int nand_change_read_column_op(struct nand_chip *n,unsigned col,void *dst,unsigned len,bool force)
{ sunxi_nfc_select_chip(n,n->cur_cs); return transfer(n,col,dst,len); }
static int nand_prog_page_begin_op(struct nand_chip *n,int page,unsigned col,const void *data,unsigned len)
{
    assert(!len && col==0); row=page; column=0; program_pending=true;
    memset(staging,255,sizeof(staging)); sunxi_nfc_select_chip(n,n->cur_cs);
    return flow_event(FP_BEGIN);
}
static int nand_change_write_column_op(struct nand_chip *n,unsigned col,const void *data,unsigned len,bool force)
{ assert(!len && col<=sizeof(staging)); column=col; sunxi_nfc_select_chip(n,n->cur_cs); return flow_event(FP_COLUMN); }
static int sunxi_nfc_write_buf(struct nand_chip *n,const u8 *data,int len)
{
    assert(program_pending && column+len<=sizeof(staging));
    if(flow_event(FP_BUFFER)) return -ETIMEDOUT;
    memcpy(staging+column,data,len); transform(staging+column,len,transport_seed());
    advance_seed(len); column+=len; return 0;
}
static int nand_prog_page_end_op(struct nand_chip *n)
{
    assert(program_pending && !flow.mapped && !flow.active &&
           !(readl(controller.regs+NFC_REG_ECC_CTL)&(NFC_ECC_EN|NFC_RANDOM_EN(&controller))));
    if(flow_event(FP_CONFIRM)) return -ETIMEDOUT;
    for(unsigned i=0;i<sizeof(staging);i++) flash[target][row][i]&=staging[i];
    program_pending=false; return 0;
}
static int sunxi_nfc_dma_op_prepare(struct sunxi_nfc *n,const void *buf,unsigned size,unsigned count,int direction,struct scatterlist *sg)
{
    assert(!flow.mapped && !flow.active); if(flow_event(FP_PREPARE)) return -ETIMEDOUT;
    sg->len=size*count; dma_buf=(void *)buf; dma_steps=count; dma_size=size;
    flow.mapped=true; return 0;
}
static void command(u32 cmd)
{
    struct nand_chip *n=&persistent_chip;
    bool dma=(cmd>>30)==2,write=cmd&NFC_ACCESS_DIR;
    unsigned first=0,count=dma?dma_steps:1,reg=0;
    if(dma) first=column/n->ecc.size;
    else {
        while(first<n->ecc.steps && B_PAGE+sunxi_get_oob_offset(n,&n->ecc,first)!=column) first++;
    }
    assert(first+count<=n->ecc.steps && count);
    assert(readl(controller.regs+NFC_REG_ECC_CTL)&NFC_ECC_EN);
    if(n->options&NAND_NEED_SCRAMBLING) {
        assert(random_on());
        /* Historical missing reseed must fail even on the first page. */
        assert(transport_seed()==sunxi_nfc_randomizer_state(n,row,false));
    }
    if(dma) assert(readl(controller.regs+NFC_REG_SPARE_AREA(&controller))==B_PAGE+sunxi_get_oob_offset(n,&n->ecc,first));
    u32 zero=0; memcpy(controller.regs+NFC_REG_ECC_ST,&zero,4);
    memcpy(controller.regs+persistent_caps.reg_pat_found,&zero,4);
    memset(controller.regs+persistent_caps.reg_ecc_err_cnt,0,8);
    for(unsigned i=first;i<first+count;i++) {
        unsigned len=sunxi_nfc_user_data_sz(n,i),off=B_PAGE+sunxi_get_oob_offset(n,&n->ecc,i);
        if(dma) {
            u8 *b=(u8 *)dma_buf+(i-first)*dma_size;
            if(write) { memcpy(staging+i*n->ecc.size,b,n->ecc.size); transform(staging+i*n->ecc.size,n->ecc.size,transport_seed()); }
            else { memcpy(b,flash[target][row]+i*n->ecc.size,n->ecc.size); transform(b,n->ecc.size,transport_seed()); }
        }
        u8 user[32];
        if(write) memcpy(user,controller.regs+persistent_caps.reg_user_data+reg,len);
        else memcpy(user,flash[target][row]+off,len);
        if(random_on()) sunxi_nfc_randomize_buf(sunxi_nfc_randomizer_state(n,row,true),user,len);
        if(write) {
            memcpy(staging+off,user,len);
            memset(staging+off+len,0x5b,n->ecc.bytes); /* opaque parity */
        } else memcpy(controller.regs+persistent_caps.reg_user_data+reg,user,len);
        reg+=len;
    }
    if(!write && !memchr_inv(flash[target][row],255,sizeof(staging))) {
        u32 mask=(1U<<count)-1;
        u32 pat=persistent_caps.reg_pat_found==NFC_REG_ECC_ST?mask<<16:mask;
        memcpy(controller.regs+persistent_caps.reg_pat_found,&pat,4);
        memcpy(controller.regs+persistent_caps.reg_pat_id,&mask,4);
    }
    if((int)row==ecc_error_page) { u32 err=BIT(0); memcpy(controller.regs+NFC_REG_ECC_ST,&err,4); }
    column=B_PAGE+sunxi_get_oob_offset(n,&n->ecc,first+count);
    flow.active=flow.mapped; advance_seed(1024); command_count++;
}
static void writel(u32 value,void *ptr)
{
    ptrdiff_t off=(u8 *)ptr-controller.regs;
    assert(off>=0 && off+4<=sizeof(controller.regs) && !(off%4));
    mmio_writes[off/4]++; memcpy(ptr,&value,4);
    if(off==NFC_REG_CMD) command(value);
}
void driver_reset(unsigned format,bool variable)
{
    free(persistent_chip.user_data_bytes); memset(&flow,0,sizeof(flow)); flow.enabled=true;
    memset(flash,255,sizeof(flash)); memset(controller.regs,0,sizeof(controller.regs));
    persistent_caps=caps(variable); persistent_caps.random_en_mask=variable?BIT(5):BIT(9);
    persistent_caps.ecc_strengths=variable?sunxi_ecc_strengths_h6:sunxi_ecc_strengths_a10;
    persistent_caps.nstrengths=variable?ARRAY_SIZE(sunxi_ecc_strengths_h6):ARRAY_SIZE(sunxi_ecc_strengths_a10);
    persistent_caps.ecc_mode_mask=variable?GENMASK(15,8):GENMASK(15,12);
    persistent_caps.has_ecc_block_512=!variable;
    controller.caps=&persistent_caps; controller.dmac=NULL; controller.use_mdma=false;
    persistent_chip=chip(2,56,B_SPARE);
    persistent_chip.base.requirements=(struct nand_ecc_props){1024,32};
    persistent_chip.options=format?NAND_NEED_SCRAMBLING:0;
    vendor_property=format==2;
    assert(!sunxi_nand_attach_chip(&persistent_chip)); persistent_chip.ecc.steps=2;
    assert(persistent_chip.ecc.write_subpage==sunxi_nfc_hw_ecc_write_subpage);
    fail_page=ecc_error_page=-1; command_count=operation_count=0;
}
int driver_region(int section,unsigned *off,unsigned *len)
{
    struct mtd_oob_region r; int ret=sunxi_nand_ooblayout_free(&persistent_chip.mtd,section,&r);
    if(!ret) { *off=r.offset; *len=r.length; } return ret;
}
int driver_ecc_region(int section,unsigned *off,unsigned *len)
{
    struct mtd_oob_region r; int ret=sunxi_nand_ooblayout_ecc(&persistent_chip.mtd,section,&r);
    if(!ret) { *off=r.offset; *len=r.length; } return ret;
}
int driver_io(bool write,bool dma,bool raw,unsigned cs,unsigned page,void *data,void *oob,
              bool need_oob,unsigned *corrected,unsigned *failed)
{
    struct nand_chip *n=&persistent_chip;
    assert(page<B_PAGES && cs<B_TARGETS); n->cur_cs=cs;
    if(need_oob && write) memcpy(n->oob_poi,oob,B_SPARE);
    else memset(n->oob_poi,255,B_SPARE);
    unsigned c=n->mtd.ecc_stats.corrected,f=n->mtd.ecc_stats.failed;
    int ret;
    if(raw) {
        assert(!(readl(controller.regs+NFC_REG_ECC_CTL)&(NFC_ECC_EN|NFC_RANDOM_EN(&controller))));
        sunxi_nfc_select_chip(n,cs); row=page;
        if((int)page==fail_page) ret=-EIO;
        else {
            if(write) {
                if(data) for(unsigned i=0;i<B_PAGE;i++) flash[cs][page][i]&=((u8 *)data)[i];
                if(need_oob) for(unsigned i=0;i<B_SPARE;i++) flash[cs][page][B_PAGE+i]&=((u8 *)oob)[i];
            } else {
                if(data) memcpy(data,flash[cs][page],B_PAGE);
                if(need_oob) memcpy(n->oob_poi,flash[cs][page]+B_PAGE,B_SPARE);
            }
            ret=0;
        }
    } else {
        n->ecc.read_page=dma?sunxi_nfc_hw_ecc_read_page_dma:sunxi_nfc_hw_ecc_read_page;
        n->ecc.write_page=dma?sunxi_nfc_hw_ecc_write_page_dma:sunxi_nfc_hw_ecc_write_page;
        if(write) ret=data?n->ecc.write_page(n,data,need_oob,page):n->ecc.write_oob(n,page);
        else ret=data?n->ecc.read_page(n,data,need_oob,page):n->ecc.read_oob(n,page);
    }
    if(!write && need_oob) memcpy(oob,n->oob_poi,B_SPARE);
    *corrected=n->mtd.ecc_stats.corrected-c; *failed=n->mtd.ecc_stats.failed-f;
    assert(!(readl(controller.regs+NFC_REG_ECC_CTL)&(NFC_ECC_EN|NFC_RANDOM_EN(&controller))) && !flow.mapped && !flow.active);
    operation_count++; return ret;
}
void driver_faults(int page,int ecc) { fail_page=page; ecc_error_page=ecc; }
void driver_erase(unsigned first,unsigned count)
{ assert(first+count<=B_TARGETS*B_PAGES); memset((u8 *)flash+first*(B_PAGE+B_SPARE),255,count*(B_PAGE+B_SPARE)); }
void driver_finish(void)
{ assert(command_count && operation_count); free(persistent_chip.user_data_bytes); persistent_chip.user_data_bytes=NULL; }
