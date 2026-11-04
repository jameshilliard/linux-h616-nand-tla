/* SPDX-License-Identifier: GPL-2.0-or-later */
static unsigned offset(const void *p)
{ ptrdiff_t o=(const u8 *)p-(const u8 *)nfc.regs; assert(o>=0 && o<0x1000 && !(o%4)); return o; }
static u32 readl(const void *p) { return media_read_reg(offset(p)); }
static void writel(u32 v, void *p) { media_write_reg(offset(p),v); }
static void memcpy_toio(void *p,const void *b,size_t n) { memcpy(p,b,n); }
static void memcpy_fromio(void *b,const void *p,size_t n) { memcpy(b,p,n); }
static unsigned wait_for_completion_timeout(struct completion *c,unsigned timeout)
{ sunxi_nfc_interrupt(1,&nfc); return c->done; }
static void disable_irq(int irq) { hw.irq_disabled=true; }
static const struct nand_controller_ops transport_ops={ .exec_op=sunxi_nfc_exec_op };
void wire_reset(void *regs, bool h616)
{
    memset(&nfc,0,sizeof(nfc)); memset(&chip,0,sizeof(chip)); memset(&hw,0,sizeof(hw));
    caps.sram_size=h616?8192:1024; caps.reg_spare_area=h616?0x114:0xa0;
    nfc.regs=regs; nfc.caps=&caps; nfc.ops=&transport_ops;
    chip.nand.controller=&nfc; chip.nand.base.ntargets=2;
    chip.nand.mtd=(struct mtd_info){.writesize=2048,.oobsize=256,.erasesize=131072};
    chip.nand.page_shift=11; chip.nsels=2; chip.clk_rate=24000000;
    chip.sels[0]=(struct sunxi_nand_chip_sel){0,0};
    chip.sels[1]=(struct sunxi_nand_chip_sel){1,1};
    media_wire_reset();
}
int wire_select(unsigned cs)
{
    chip.nand.cur_cs=cs;
    struct nand_operation op={.cs=cs};
    return nand_exec_op(&chip.nand,&op);
}
int wire_read(unsigned cs,unsigned page,unsigned col,void *b,unsigned len)
{ chip.nand.cur_cs=cs; return nand_read_page_op(&chip.nand,page,col,b,len); }
int wire_read_column(unsigned cs,unsigned col,void *b,unsigned len)
{ chip.nand.cur_cs=cs; return nand_change_read_column_op(&chip.nand,col,b,len,false); }
int wire_begin(unsigned cs,unsigned page,unsigned col,const void *b,unsigned len)
{ chip.nand.cur_cs=cs; return nand_prog_page_begin_op(&chip.nand,page,col,b,len); }
int wire_write_column(unsigned cs,unsigned col,const void *b,unsigned len)
{ chip.nand.cur_cs=cs; return nand_change_write_column_op(&chip.nand,col,b,len,false); }
int wire_write_buf(unsigned cs,const void *b,unsigned len)
{ chip.nand.cur_cs=cs; return sunxi_nfc_write_buf(&chip.nand,b,len); }
int wire_read_buf(unsigned cs,void *b,unsigned len)
{ chip.nand.cur_cs=cs; return sunxi_nfc_read_buf(&chip.nand,b,len); }
int wire_end(unsigned cs) { chip.nand.cur_cs=cs; return nand_prog_page_end_op(&chip.nand); }
int wire_raw(bool write,unsigned cs,unsigned page,void *data,void *oob,bool need_oob)
{
    chip.nand.cur_cs=cs; chip.nand.oob_poi=oob;
    if(data) return write?nand_write_page_raw(&chip.nand,data,need_oob,page):
                         nand_read_page_raw(&chip.nand,data,need_oob,page);
    assert(need_oob);
    if(!write) return nand_read_oob_op(&chip.nand,page,0,oob,256);
    int ret=nand_prog_page_begin_op(&chip.nand,page,2048,oob,256);
    return ret?ret:nand_prog_page_end_op(&chip.nand);
}

extern void driver_reset(unsigned, bool),driver_finish(void);
extern int driver_attach_check(unsigned,unsigned,unsigned,unsigned,bool,bool,bool);
static bool retry_micron;
static int retry_fail_mode;
static unsigned retry_fail_command, retry_fail_accepted;
static struct hynix_read_retry *retry_table;
static struct hynix_nand retry_vendor;
void wire_retry_start(bool micron, int fail_mode, unsigned fail_command, unsigned accepted)
{
    static const u8 regs[]={0xa7,0xad,0xae,0xaf};
    if(!retry_table) { retry_table=malloc(sizeof(*retry_table)+8); assert(retry_table); }
    retry_table->nregs=4; retry_table->regs=regs;
    for(unsigned i=0;i<8;i++) retry_table->values[i]=i<4?0:(0x21+i);
    retry_vendor.read_retry=retry_table; chip.nand.manufacturer=&retry_vendor;
    retry_micron=micron; retry_fail_mode=fail_mode;
    retry_fail_command=fail_command; retry_fail_accepted=accepted;
}
int wire_retry_setup(unsigned mode)
{
    media_wire_fault(mode==retry_fail_mode?retry_fail_command:0,retry_fail_accepted);
    int ret=retry_micron?micron_nand_setup_read_retry(&chip.nand,mode):
                         hynix_nand_setup_read_retry(&chip.nand,mode);
    media_wire_fault(0,0);
    return ret;
}
void wire_tests(void)
{
    assert(!driver_attach_check(512,128,512,17,false,false,false));
    assert(!driver_attach_check(2048,256,1024,17,false,false,false));
    unsigned count=0;
    for(unsigned h616=0;h616<2;h616++)
    for(unsigned cs=0;cs<2;cs++)
    for(unsigned rb=0;rb<2;rb++) {
        driver_reset(0,h616);
        chip.sels[cs].rb=rb?cs:-1;
        u8 got[2304], want[2304];
        for(unsigned i=0;i<sizeof(want);i++) want[i]=(i*7+31)&255;
        assert(!wire_begin(cs,3,0,want,sizeof(want)));
        assert(!wire_end(cs));
        memset(got,0,sizeof(got)); assert(!wire_read(cs,3,0,got,sizeof(got)));
        assert(!memcmp(got,want,sizeof(got)));
        assert(!wire_read_column(cs,2048,got,256)); assert(!memcmp(got,want+2048,256));
        unsigned before=media_wire_count();
        assert(wire_read(cs,3,2300,got,5)==-EINVAL && media_wire_count()==before);
        struct nand_op_instr status[]={NAND_OP_CMD(NAND_CMD_STATUS,0)};
        struct nand_operation op=NAND_OPERATION(cs,status);
        assert(!nand_check_op(&chip.nand,&op) && media_wire_count()==before);
        media_wire_fault(0,0); assert(!wire_read(cs,3,0,got,sizeof(got)));
        unsigned commands=media_wire_count();
        for(unsigned fail=1;fail<=commands+1;fail++) {
            media_wire_fault(fail,0); memset(got,0x39,sizeof(got));
            int ret=wire_read(cs,3,0,got,sizeof(got));
            if(fail<=commands) assert(ret==-ETIMEDOUT); else assert(!ret);
            media_wire_fault(0,0);
            assert(!wire_read(cs,3,0,got,sizeof(got)) && !memcmp(got,want,sizeof(got)));
            count++;
        }
    }
    /* Actual Hynix enter/register/apply and Micron SET FEATURES callbacks. */
    static const u8 rr_regs[]={0xa7,0xad,0xae,0xaf};
    struct hynix_read_retry *rr=malloc(sizeof(*rr)+8);
    assert(rr); rr->nregs=4; rr->regs=rr_regs;
    for(unsigned i=0;i<8;i++) rr->values[i]=i<4?0:(0x21+i);
    struct hynix_nand vendor={.read_retry=rr};
    unsigned failures=0, partial=0, restores=0;
    for(unsigned micron=0;micron<2;micron++)
    for(unsigned fail=0;fail<=(micron?1:6);fail++)
    for(unsigned accepted=0;accepted<2;accepted++) {
        driver_reset(0,true); chip.nand.manufacturer=&vendor;
        int (*setup)(struct nand_chip *,int)=micron?micron_nand_setup_read_retry:hynix_nand_setup_read_retry;
        media_wire_fault(fail,accepted?4:0);
        int ret=setup(&chip.nand,1);
        assert((ret<0)==(fail!=0));
        u8 staged[4],active[4]; unsigned applies,mode;
        media_retry_snapshot(staged,active,&applies,&mode);
        if(ret) { failures++; if(memcmp(staged,(u8[4]){0},4)) partial++; }
        else { assert(active[0]==(micron?1:rr->values[4])); }
        /* Explicit restore is a caller action, not an implicit rollback. */
        media_wire_fault(0,0); assert(!setup(&chip.nand,0));
        media_retry_snapshot(staged,active,&applies,&mode);
        assert(!memcmp(active,(u8[4]){0},4));
        assert(!setup(&chip.nand,1));
        media_wire_fault(1,0); assert(setup(&chip.nand,0)==-ETIMEDOUT);
        media_retry_snapshot(staged,active,&applies,&mode);
        assert(active[0]==(micron?1:rr->values[4])); restores++;
        media_wire_fault(0,0); assert(!setup(&chip.nand,0));
    }
    free(rr);
    driver_reset(0,false);
    u8 dma_data[1024]; struct scatterlist sg;
    hw.prep_fail=true;
    assert(sunxi_nfc_dma_op_prepare(&nfc,dma_data,1024,1,DMA_FROM_DEVICE,&sg)==-EINVAL);
    assert(hw.maps==1 && hw.unmaps==1 && !hw.submit);
    hw.prep_fail=false;
    assert(!sunxi_nfc_dma_op_prepare(&nfc,dma_data,1024,1,DMA_FROM_DEVICE,&sg));
    sunxi_nfc_dma_op_cleanup(&nfc,DMA_FROM_DEVICE,&sg);
    assert(hw.maps==2 && hw.unmaps==2 && hw.submit==1);
    free(retry_table); retry_table=NULL;
    printf("Transport: %u command fault/recovery cases; retry: %u injected failures, %u partial states, %u failed restores\n",
           count,failures,partial,restores);
    puts("ALL CHECKS PASSED");
}
