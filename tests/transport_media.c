/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Normal commands are decoded from registers written by actual exec_subop.
 * Parameter writes take effect before their completion interrupt; neither
 * timeout nor controller reset is assumed to roll back NAND-chip state. */
static unsigned wire_mode, wire_count, wire_fail, wire_accepted, wire_base;
static unsigned retry_mode, retry_address, retry_applies;
static u8 retry_staged[4], retry_active[4];
uint32_t media_read_reg(unsigned o) { return readl(controller.regs+o); }
void media_write_reg(unsigned o,uint32_t v) { writel(v,controller.regs+o); }
void media_wire_reset(void)
{
    wire_mode=wire_count=wire_fail=wire_accepted=wire_base=0;
    retry_mode=retry_address=retry_applies=0;
    memset(retry_staged,0,4); memset(retry_active,0,4);
    program_pending=false;
}
void media_wire_fault(unsigned n,unsigned accepted)
{ wire_count=0; wire_fail=n; wire_accepted=accepted; }
unsigned media_wire_count(void) { return wire_count; }
void media_retry_snapshot(uint8_t *staged,uint8_t *active,unsigned *applies,unsigned *mode)
{ memcpy(staged,retry_staged,4); memcpy(active,retry_active,4); *applies=retry_applies; *mode=retry_mode; }
static void wire_opcode(unsigned op)
{
    switch(op) {
    case 0x00: wire_mode=0; wire_base=0; break;
    case 0x01: wire_mode=0; wire_base=256; break;
    case 0x50: wire_mode=0; wire_base=B_PAGE; break;
    case 0x30: case 0xe0: break;
    case 0x05: wire_mode=0x05; break;
    case 0x85: wire_mode=0x85; break;
    case 0x80:
        wire_mode=0x80; program_pending=true; memset(staging,255,sizeof(staging)); break;
    case 0x10:
        assert(program_pending && !flow.mapped && !flow.active);
        for(unsigned i=0;i<sizeof(staging);i++) flash[target][row][i]&=staging[i];
        program_pending=false; break;
    case 0x70: wire_mode=0x70; break;
    case 0x36: wire_mode=0x36; retry_mode=0x36; break;
    case 0x16:
        memcpy(retry_active,retry_staged,4); retry_applies++; retry_mode=0; break;
    case 0xef: wire_mode=0xef; break;
    default: assert(!"unsupported wire opcode");
    }
}
static void wire_command(u32 cmd)
{
    wire_count++;
    bool fail=wire_fail && wire_count==wire_fail;
    if(fail && !wire_accepted) return;
    target=(readl(controller.regs)&0x07000000)>>24;
    assert(target<B_TARGETS);
    if(cmd&BIT(22)) wire_opcode(cmd&255);
    if(cmd&BIT(19)) {
        unsigned n=((cmd>>16)&7)+1; u8 a[8];
        u32 low=readl(controller.regs+0x14),high=readl(controller.regs+0x18);
        for(unsigned i=0;i<n;i++) a[i]=i<4?low>>(8*i):high>>(8*(i-4));
        if(wire_mode==0x36 || wire_mode==0xef) { assert(n==1); retry_address=a[0]; }
        else {
            assert(n>=2); column=a[0]+256*a[1]+wire_base;
            if(n>2) { assert(n==4 || n==5); row=a[2]+256*a[3]; if(n==5) row+=65536*a[4]; }
            assert(row<B_PAGES && column<=B_PAGE+B_SPARE);
        }
    }
    bool write=cmd&BIT(20);
    /* The second read command precedes DATA_IN. PAGEPROG follows DATA_OUT. */
    if((cmd&BIT(24)) && !write) wire_opcode(readl(controller.regs+0x28)&255);
    if(cmd&BIT(21)) {
        unsigned n=readl(controller.regs+0x20),limit=fail?min(n,wire_accepted):n;
        u8 *sram=controller.regs+0x400;
        if(wire_mode==0x70 && !write) { assert(n==1); sram[0]=0x40; }
        else if(wire_mode==0x36 && write) {
            static const u8 regs[]={0xa7,0xad,0xae,0xaf}; unsigned i;
            for(i=0;i<4 && regs[i]!=retry_address;i++); assert(i<4 && n==1);
            if(limit) retry_staged[i]=sram[0];
        } else if(wire_mode==0xef && write) {
            assert(n==4 && retry_address==0x89);
            memcpy(retry_staged,sram,limit); memcpy(retry_active,retry_staged,4); retry_applies++;
        } else {
            assert(column+n<=sizeof(staging));
            if(write) {
                assert(program_pending); memcpy(staging+column,sram,limit);
                transform(staging+column,limit,transport_seed()); advance_seed(limit);
            } else {
                memcpy(sram,flash[target][row]+column,limit);
                transform(sram,limit,transport_seed()); advance_seed(limit);
            }
            column+=n;
        }
    }
    if((cmd&BIT(24)) && write) wire_opcode(readl(controller.regs+0x2c)&255);
    if(!fail) {
        u32 status=readl(controller.regs+0x04)|BIT(1);
        memcpy(controller.regs+0x04,&status,4);
    }
}
