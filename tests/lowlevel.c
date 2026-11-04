/* SPDX-License-Identifier: GPL-2.0-or-later */
static unsigned regoff(const void *p)
{
    ptrdiff_t off=(const u8 *)p-registers;
    assert(off>=0 && off+4<=(ptrdiff_t)sizeof(registers) && !(off%4));
    return off;
}
static u32 load(unsigned off) { u32 v; memcpy(&v,registers+off,4); return v; }
static void store(unsigned off,u32 v) { memcpy(registers+off,&v,4); }
static u16 shift_bit(u16 s)
{ return (s>>1) | (((s&1)^((s>>1)&1))<<14); }
static u16 shift_n(u16 s,unsigned n) { while(n--) s=shift_bit(s); return s; }
static u16 stream_state;
static u32 readl(const void *p)
{
    unsigned off=regoff(p);
    hw.reads++;
    if (off==NFC_REG_CTL && (load(off)&NFC_RESET) && !hw.reset_fail) {
        store(off,0); store(NFC_REG_ST,0); hw.active=false;
    }
    if (off==NFC_REG_ST && hw.fifo_busy) return load(off)|NFC_CMD_FIFO_STATUS;
    return load(off);
}
static void writel(u32 value, void *p)
{
    unsigned off=regoff(p);
    hw.writes++; hw.serial++;
    hw.reg_writes[off/4]++;
    if (off==NFC_REG_ST || off==NFC_REG_H6_MDMA_STA) {
        store(off,load(off)&~value); return;
    }
    store(off,value);
    if (off==NFC_REG_ECC_CTL && (value&caps.random_en_mask))
        stream_state=shift_n((value>>16)&0x7fff,15);
    if (off==NFC_REG_H6_MDMA_DLBA_REG) {
        hw.doorbell=hw.serial;
        assert(hw.barrier && hw.barrier<hw.doorbell);
        hw.published=true;
    }
    if (off!=NFC_REG_CMD) return;
    hw.commands++;
    if(value&NFC_SEND_ADR) hw.address_cycles+=1+((value>>16)&7);
    bool fail=hw.commands==hw.fail_command;
    if (value&NFC_DATA_TRANS) {
        unsigned n=load(NFC_REG_CNT);
        assert(n<=(unsigned)caps.sram_size && hw.column+n<=sizeof(media[0]));
        unsigned limit=fail ? min(n,hw.partial_bytes) : n;
        for(unsigned i=0;i<limit;i++) {
            u8 mask=(load(NFC_REG_ECC_CTL)&caps.random_en_mask) ? (u8)stream_state : 0;
            if(value&NFC_ACCESS_DIR) media[hw.row][hw.column+i]&=registers[NFC_RAM0_BASE+i]^mask;
            else registers[NFC_RAM0_BASE+i]=media[hw.row][hw.column+i]^mask;
            stream_state=shift_n(stream_state,8);
        }
        hw.column+=n;
    }
    if(!fail) store(NFC_REG_ST,load(NFC_REG_ST)|NFC_CMD_INT_FLAG);
}
static void memcpy_toio(void *dst,const void *src,size_t n)
{
    assert((u8 *)dst>=registers+NFC_RAM0_BASE &&
           (u8 *)dst+n<=registers+NFC_RAM0_BASE+caps.sram_size);
    memcpy(dst,src,n);
}
static void memcpy_fromio(void *dst,const void *src,size_t n)
{
    assert((const u8 *)src>=registers+NFC_RAM0_BASE &&
           (const u8 *)src+n<=registers+NFC_RAM0_BASE+caps.sram_size);
    memcpy(dst,src,n);
}
static unsigned wait_for_completion_timeout(struct completion *c,unsigned timeout)
{
    hw.wait_calls++;
    if(hw.wait_mode==1) return 0;
    hw.irq_calls++;
    sunxi_nfc_interrupt(1,&nfc);
    return hw.wait_mode==2 ? 0 : c->done;
}
static void disable_irq(int irq)
{
    hw.disable++; hw.irq_disabled=true;
    /* mode 2: a handler already acknowledged CMD, DMA arrives while draining. */
    if(hw.wait_mode==2) {
        store(NFC_REG_ST,load(NFC_REG_ST)|NFC_DMA_INT_FLAG);
        sunxi_nfc_interrupt(irq,&nfc);
    }
}
static void reset_fixture(unsigned backend)
{
    memset(&hw,0,sizeof(hw)); memset(&nfc,0,sizeof(nfc));
    memset(registers,0,sizeof(registers)); memset(&desc,0xa5,sizeof(desc));
    memset(&chip,0,sizeof(chip)); memset(media,0xff,sizeof(media));
    assert(backend<ARRAY_SIZE(source_caps));
    caps=source_caps[backend];
    nfc.regs=registers; nfc.caps=&caps; nfc.use_mdma=backend!=0;
    nfc.mdma_desc=backend==2 ? &desc : NULL; nfc.mdma_desc_dma=0x12345000;
    hw.map_address=0x9abc0000;
    chip.nand.controller=&nfc; chip.nand.mtd=(struct mtd_info){2048,2048*64,1024};
    chip.nand.page_shift=11; chip.nsels=2;
    chip.sels[0]=(struct sunxi_nand_chip_sel){0,0};
    chip.sels[1]=(struct sunxi_nand_chip_sel){2,1};
    chip.clk_rate=24000000; chip.timing_ctl=0x100; chip.timing_cfg=0x777;
}
static void dma_checks(void)
{
    unsigned runs=0;
    for(unsigned backend=0;backend<3;backend++)
    for(unsigned direction=0;direction<2;direction++)
    for(unsigned fault=0;fault<4;fault++)
    for(unsigned chunks=1;chunks<=32;chunks*=2) {
        reset_fixture(backend);
        if(chunks>caps.max_ecc_steps) continue;
        hw.map_fail=fault==1; hw.prep_fail=fault==2; hw.submit_fail=fault==3;
        store(NFC_REG_CTL,0x80008001);
        struct scatterlist sg;
        int ret=sunxi_nfc_dma_op_prepare(&nfc,media,1024,chunks,direction,&sg);
        bool fail=fault==1 || (!backend && (fault==2 || fault==3));
        assert((ret<0)==fail);
        if(!fail) {
            assert(hw.mapped && (load(NFC_REG_CTL)&NFC_RAM_METHOD));
            assert(load(NFC_REG_CTL)==((0x80008001|NFC_RAM_METHOD)&
                                     ~(backend ? NFC_DMA_TYPE_NORMAL : 0)));
            assert(load(NFC_REG_CNT)==1024);
            assert(load(NFC_REG_SECTOR_NUM)==(backend==2 ? GENMASK(chunks-1,0) : chunks));
            if(backend==2) {
                const u8 expected[]={12,0,0,0, (1024*chunks)&255,(1024*chunks)>>8,0,0,
                    0,0,0xbc,0x9a, 0,0x50,0x34,0x12};
                assert(!memcmp(&desc,expected,sizeof(expected)));
                assert(hw.published && hw.barrier<hw.doorbell);
            } else if(backend==1) {
                assert(load(NFC_REG_MDMA_ADDR)==hw.map_address);
                assert(load(NFC_REG_MDMA_CNT)==1024*chunks);
            } else assert(hw.prep==1 && hw.submit==1);
            sunxi_nfc_dma_op_cleanup(&nfc,direction,&sg);
        }
        assert(!hw.mapped && hw.maps==hw.unmaps && !hw.unsafe);
        assert(hw.map_direction==(enum dma_data_direction)direction);
        if(hw.unmaps) assert(hw.unmap_direction==(enum dma_data_direction)direction);
        if(hw.prep) assert(hw.prep_direction==(direction==DMA_FROM_DEVICE ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV));
        assert(!(load(NFC_REG_CTL)&NFC_RAM_METHOD));
        for(unsigned off=0;off<sizeof(registers);off+=4) if(hw.reg_writes[off/4]) {
            bool common=off==NFC_REG_CTL || off==NFC_REG_CNT || off==NFC_REG_SECTOR_NUM;
            bool legacy=backend==1 && (off==NFC_REG_MDMA_ADDR || off==NFC_REG_MDMA_CNT);
            bool h6=backend==2 && (off==NFC_REG_H6_MDMA_DLBA_REG || off==NFC_REG_H6_MDMA_STA);
            assert(common || legacy || h6);
        }
        /* On external DMA this bit is not owned by the driver's MDMA path. */
        if(!backend) assert(load(NFC_REG_CTL)&NFC_DMA_TYPE_NORMAL);
        runs++;
    }
    static const unsigned lengths[]={1,7,8,511,512,1024,65528,65535,65536};
    static u8 dma_buffer[65536];
    for(unsigned l=0;l<ARRAY_SIZE(lengths);l++)
    for(unsigned alignment=0;alignment<8;alignment++) {
        reset_fixture(2); hw.map_address+=alignment;
        struct scatterlist sg;
        int ret=sunxi_nfc_dma_op_prepare(&nfc,dma_buffer,lengths[l],1,DMA_TO_DEVICE,&sg);
        bool valid=lengths[l]<=65535 && !(lengths[l]%8) && !(alignment%4);
        assert((ret==0)==valid);
        if(valid) sunxi_nfc_dma_op_cleanup(&nfc,DMA_TO_DEVICE,&sg);
        assert(hw.maps==hw.unmaps && !hw.mapped);
        runs++;
    }
    for(unsigned backend=0;backend<3;backend++)
    for(unsigned fail=0;fail<2;fail++) {
        reset_fixture(backend); hw.active=true;
        hw.reset_fail=hw.terminate_fail=fail;
        sunxi_nfc_dma_op_abort(&nfc);
        assert(hw.active==(bool)fail);
        if(backend) assert(!load(NFC_REG_ECC_CTL));
    }
    printf("DMA backends: %u map/prepare/submit/alignment/length cases; abort contract witnesses reproduced\n",runs);
}
static void dma_init_checks(void)
{
    unsigned runs=0;
    for(unsigned backend=0;backend<3;backend++)
    for(unsigned fail=0;fail<6;fail++) {
        reset_fixture(backend); nfc.use_mdma=false; nfc.mdma_desc=NULL; nfc.dmac=NULL;
        hw.mask_fail=fail==1; hw.alloc_fail=fail==2;
        hw.channel_error=fail==3 ? -EIO : fail==4 ? -EPROBE_DEFER : 0;
        hw.config_error=fail==5 ? -EIO : 0;
        struct resource r={.start=0x1c03000};
        int ret=sunxi_nfc_dma_init(&nfc,&r);
        assert(ret==(!backend && fail==4 ? -EPROBE_DEFER : 0));
        if(backend==2) {
            assert(hw.dma_mask==UINT32_MAX);
            assert(nfc.use_mdma==!(fail==1 || fail==2));
            assert(!!nfc.mdma_desc==nfc.use_mdma);
        } else if(backend==1) assert(nfc.use_mdma);
        else if(fail==3) assert(!nfc.dmac);
        else if(!ret) {
            assert(nfc.dmac && !nfc.use_mdma);
            assert(configured.src_addr==r.start+caps.reg_io_data && configured.dst_addr==configured.src_addr);
            assert(configured.src_addr_width==4 && configured.dst_addr_width==4);
            assert(configured.src_maxburst==caps.dma_maxburst && configured.dst_maxburst==caps.dma_maxburst);
        }
        runs++;
    }
    printf("source DMA initialization: %u mask/allocation/channel/defer/configuration cases\n",runs);
    puts("SOURCE CONTRACT WITNESS: external dmaengine_slave_config failure is ignored by dma_init");
}
static void irq_checks(void)
{
    unsigned runs=0;
    for(unsigned events=1;events<=7;events++)
    for(unsigned status=0;status<=7;status++) {
        reset_fixture(2); store(NFC_REG_ST,status|BIT(8)); store(NFC_REG_INT,events);
        int ret=sunxi_nfc_interrupt(1,&nfc);
        assert(ret==((events&status) ? IRQ_HANDLED : IRQ_NONE));
        assert(nfc.complete.done==((events&status)==events));
        assert(load(NFC_REG_INT)==((events&status) ? events&~status : events));
        assert(load(NFC_REG_ST)&BIT(8));
        runs++;
    }
    for(unsigned poll=0;poll<2;poll++)
    for(unsigned events=1;events<=7;events++)
    for(unsigned status=0;status<=7;status++)
    for(unsigned timeout=0;timeout<2;timeout++) {
        reset_fixture(2); store(NFC_REG_ST,status|BIT(8)); hw.wait_mode=timeout;
        int ret=sunxi_nfc_wait_events(&nfc,events,poll,0);
        assert(ret==(((status&events)==events) ? 0 : -ETIMEDOUT));
        assert(!load(NFC_REG_INT) && !(load(NFC_REG_ST)&events));
        assert(load(NFC_REG_ST)&BIT(8));
        assert(hw.disable==hw.enable && !hw.irq_disabled);
        runs++;
    }
    reset_fixture(2); hw.wait_mode=2; store(NFC_REG_ST,NFC_CMD_INT_FLAG);
    assert(!sunxi_nfc_wait_events(&nfc,NFC_CMD_INT_FLAG|NFC_DMA_INT_FLAG,false,17));
    assert(hw.disable==1 && hw.enable==1 && !load(NFC_REG_ST));
    reset_fixture(2);
    assert(sunxi_nfc_wait_events(&nfc,BIT(3),false,0)==-EINVAL && !hw.writes);
    hw.fifo_busy=true; assert(sunxi_nfc_wait_cmd_fifo_empty(&nfc)==-ETIMEDOUT);
    hw.fifo_busy=false; assert(!sunxi_nfc_wait_cmd_fifo_empty(&nfc));
    printf("IRQ/poll: %u status/mask/timeout cases, W1C preservation and drained partial completion\n",runs);
}
static void raw_media_checks(void)
{
    static const unsigned sizes[]={0,1,63,64,1023,1024,1025,8191,8192,8193,16385};
    u8 input[17000], output[17000];
    unsigned runs=0;
    for(unsigned backend=0;backend<3;backend+=2)
    for(unsigned scramble=0;scramble<2;scramble++)
    for(unsigned page=0;page<2;page++)
    for(unsigned l=0;l<ARRAY_SIZE(sizes);l++) {
        reset_fixture(backend); hw.row=page; unsigned len=sizes[l];
        for(unsigned i=0;i<sizeof(input);i++) input[i]=(i*13+(i>>8)*71+page*101)&255;
        memset(output,0xa5,sizeof(output));
        chip.nand.options=scramble ? NAND_NEED_SCRAMBLING : 0;
        assert(!sunxi_nfc_randomizer_write_buf(&chip.nand,input,len,false,page));
        assert(!(load(NFC_REG_ECC_CTL)&caps.random_en_mask));
        u16 state=shift_n(sunxi_nfc_randomizer_page_seeds[page],15);
        for(unsigned i=0;i<len;i++) {
            assert(media[page][i]==(u8)(input[i]^(scramble ? (u8)state : 0)));
            state=shift_n(state,8);
        }
        assert(media[page][len]==0xff && media[!page][0]==0xff);
        hw.column=0;
        assert(!sunxi_nfc_randomizer_read_buf(&chip.nand,output,len,false,page));
        assert(!memcmp(input,output,len) && output[len]==0xa5);
        /* A repeated NAND program cannot resurrect a zero bit. */
        u8 before[17000]; memcpy(before,media[page],len);
        hw.column=0; memset(input,0x5a,len);
        assert(!sunxi_nfc_randomizer_write_buf(&chip.nand,input,len,false,page));
        for(unsigned i=0;i<len;i++) assert((media[page][i]|before[i])==before[i]);
        runs++;
    }
    for(unsigned write=0;write<2;write++)
    for(unsigned fail=1;fail<=3;fail++) {
        reset_fixture(0); chip.nand.options=NAND_NEED_SCRAMBLING;
        hw.fail_command=fail; hw.partial_bytes=17;
        memset(output,0xa5,sizeof(output)); memset(input,0x33,sizeof(input));
        int ret=write ? sunxi_nfc_randomizer_write_buf(&chip.nand,input,2500,false,0) :
                        sunxi_nfc_randomizer_read_buf(&chip.nand,output,2500,false,0);
        assert(ret==-ETIMEDOUT && hw.commands==fail);
        assert(!(load(NFC_REG_ECC_CTL)&caps.random_en_mask));
        if(!write) for(unsigned i=(fail-1)*1024;i<sizeof(output);i++) assert(output[i]==0xa5);
    }
    reset_fixture(2); assert(!sunxi_nfc_read_buf(&chip.nand,NULL,1025));
    hw.fifo_busy=true;
    assert(sunxi_nfc_read_buf(&chip.nand,output,1)==-ETIMEDOUT);
    assert(sunxi_nfc_write_buf(&chip.nand,input,1)==-ETIMEDOUT);
    hw.fifo_busy=false;
    chip.ecc.ecc_ctl=0x137; sunxi_nfc_hw_ecc_enable(&chip.nand);
    assert(load(NFC_REG_ECC_CTL)==0x137); sunxi_nfc_hw_ecc_disable(&chip.nand);
    assert(!load(NFC_REG_ECC_CTL));
    printf("source raw SRAM/media: %u unique-byte two-page round trips, partial-program and failure sentinels\n",runs);
}
static void command_checks(void)
{
    u8 addresses[16], input[4096], output[4096]; unsigned runs=0;
    for(unsigned i=0;i<16;i++) addresses[i]=i*17+3;
    for(unsigned i=0;i<sizeof(input);i++) input[i]=(i*73+(i>>8))&255;
    for(unsigned direction=0;direction<2;direction++)
    for(unsigned naddr=1;naddr<=8;naddr++)
    for(unsigned ready=0;ready<2;ready++) {
        reset_fixture(2);
        struct nand_op_instr ins[5]={
            {.type=NAND_OP_CMD_INSTR,.ctx.cmd.opcode=0x80},
            {.type=NAND_OP_ADDR_INSTR,.ctx.addr={addresses,naddr}},
            {.type=NAND_OP_CMD_INSTR,.ctx.cmd.opcode=0x10},
            {.type=NAND_OP_WAITRDY_INSTR},
            {.type=direction ? NAND_OP_DATA_OUT_INSTR : NAND_OP_DATA_IN_INSTR,
                .ctx.data={.buf.in=direction ? input : output,.len=63}},
        };
        if(!ready) { ins[3]=ins[4]; }
        memcpy(media[0],input,63); if(direction) memset(media,0xff,sizeof(media));
        struct nand_subop sub={.instrs=ins,.ninstrs=ready ? 5 : 4};
        assert(!sunxi_nfc_exec_subop(&chip.nand,&sub));
        u32 low=0,high=0;
        for(unsigned j=0;j<naddr;j++) {
            if(j<4) low|=(u32)addresses[j]<<(j*8);
            else high|=(u32)addresses[j]<<((j-4)*8);
        }
        assert(load(NFC_REG_ADDR_LOW)==low && load(NFC_REG_ADDR_HIGH)==high);
        assert(((load(NFC_REG_CMD)>>16)&7)==naddr-1);
        assert(load(direction ? NFC_REG_WCMD_SET : NFC_REG_RCMD_SET)==0x10);
        assert(ready ? hw.wait_calls==1 : hw.poll_calls==2);
        assert(!memcmp(direction ? media[0] : output,input,63));
        runs++;
    }
    for(unsigned direction=0;direction<2;direction++)
    for(unsigned start=0;start<=2048;start+=1024) {
        reset_fixture(2); memset(output,0xa5,sizeof(output));
        memcpy(media[0],input+start,1024);
        if(direction) memset(media,0xff,sizeof(media));
        struct nand_op_instr ins={.type=direction ? NAND_OP_DATA_OUT_INSTR : NAND_OP_DATA_IN_INSTR,
            .ctx.data={.buf.in=direction ? input : output,.len=3072}};
        struct nand_subop sub={.instrs=&ins,.ninstrs=1,
            .first_instr_start_off=start,.last_instr_end_off=start+1024};
        assert(!sunxi_nfc_exec_subop(&chip.nand,&sub));
        assert(!memcmp(direction ? media[0] : output+start,input+start,1024));
        if(!direction) {
            for(unsigned i=0;i<start;i++) assert(output[i]==0xa5);
            assert(output[start+1024]==0xa5);
        }
        runs++;
    }
    reset_fixture(2);
    struct nand_op_instr ins={.type=NAND_OP_ADDR_INSTR,.ctx.addr={addresses,10}};
    struct nand_subop sub={.instrs=&ins,.ninstrs=1,.first_instr_start_off=8,.last_instr_end_off=10};
    assert(nand_subop_get_num_addr_cyc(&sub,0)==2);
    assert(!sunxi_nfc_exec_subop(&chip.nand,&sub));
#if FIXED_SUNXI
    assert(load(NFC_REG_CMD)&NFC_SEND_ADR);
    assert(hw.address_cycles==2);
    assert(load(NFC_REG_ADDR_LOW)==((u32)addresses[8] | (u32)addresses[9]<<8));
    assert(!load(NFC_REG_ADDR_HIGH));
#else
    assert(!(load(NFC_REG_CMD)&NFC_SEND_ADR));
    puts("SOURCE CONTRACT WITNESS: split 10-cycle address, start 8/count 2, emits no address; normal NAND reachability not established");
#endif
    reset_fixture(2);
    struct nand_op_instr three[3]={{.type=0},{.type=0},{.type=0}};
    sub=(struct nand_subop){.instrs=three,.ninstrs=3};
    assert(sunxi_nfc_exec_subop(&chip.nand,&sub)==-EINVAL && !hw.commands);
    ins=(struct nand_op_instr){.type=NAND_OP_WAITRDY_INSTR,.ctx.waitrdy.timeout_ms=91};
    sub=(struct nand_subop){.instrs=&ins,.ninstrs=1};
    assert(!sunxi_nfc_soft_waitrdy(&chip.nand,&sub) && hw.soft_wait_ms==91);
    hw.fifo_busy=true; assert(sunxi_nfc_soft_waitrdy(&chip.nand,&sub)==-ETIMEDOUT);
    assert(sunxi_nfc_exec_subop(&chip.nand,&sub)==-ETIMEDOUT);
    printf("source command/address/direction/split-data checks: %u\n",runs);
}
static void parser_checks(void)
{
    u8 buf[4096],original[4096],address[]={0,8,0,1,0};
    unsigned runs=0;
    for(unsigned ready=0;ready<2;ready++)
    for(unsigned write=0;write<2;write++)
    for(unsigned check=0;check<2;check++)
    for(unsigned fail=0;fail<4;fail++) {
        reset_fixture(2); chip.sels[0].rb=ready ? 0 : -1;
        for(unsigned i=0;i<sizeof(buf);i++) original[i]=buf[i]=(i*43+(i>>8)*3)&255;
        if(!write) { memcpy(media[0],buf,3001); memset(buf,0xa5,sizeof(buf)); }
        hw.fail_command=fail;
        struct nand_op_instr cmd={.type=NAND_OP_CMD_INSTR,.ctx.cmd.opcode=write ? 0x80 : 0};
        struct nand_op_instr addr={.type=NAND_OP_ADDR_INSTR,.ctx.addr={address,5}};
        struct nand_op_instr data={.type=write ? NAND_OP_DATA_OUT_INSTR : NAND_OP_DATA_IN_INSTR,
            .ctx.data={.buf.in=buf,.len=3001}};
        struct nand_op_instr end={.type=NAND_OP_CMD_INSTR,.ctx.cmd.opcode=write ? 0x10 : 0x30};
        struct nand_op_instr wait={.type=NAND_OP_WAITRDY_INSTR,.ctx.waitrdy.timeout_ms=103};
        struct nand_op_instr instructions[]={cmd,addr,write ? data : end,write ? end : wait,write ? wait : data};
        struct nand_operation op={.cs=0,.instrs=instructions,.ninstrs=5};
        int ret=sunxi_nfc_exec_op(&chip.nand,&op,check);
        if(check) assert(!ret && !hw.commands && !hw.writes && !hw.clock_calls);
        else if(fail) assert(ret==-ETIMEDOUT && hw.commands==fail);
        else {
            assert(!ret && hw.column==3001 && hw.address_cycles==5);
            assert(!memcmp(write ? media[0] : buf,original,3001));
            if(!write) assert(buf[3001]==0xa5);
            if(!ready) assert(hw.soft_wait_ms==103);
        }
        runs++;
    }
    reset_fixture(2);
    u8 a[10]={0,1,2,3,4,5,6,7,8,9};
    struct nand_op_instr addr={.type=NAND_OP_ADDR_INSTR,.ctx.addr={a,10}};
    struct nand_operation op={.instrs=&addr,.ninstrs=1};
    assert(!sunxi_nfc_exec_op(&chip.nand,&op,true) && !hw.writes);
    assert(!sunxi_nfc_exec_op(&chip.nand,&op,false));
#if FIXED_SUNXI
    assert(hw.commands==2 && hw.address_cycles==10);
    assert(load(NFC_REG_ADDR_LOW)==0x0908 && !load(NFC_REG_ADDR_HIGH));
    puts("FIX VERIFIED through NAND-core parser: all 10 address cycles emitted");
#else
    assert(hw.commands==2 && hw.address_cycles==8);
    puts("SOURCE CONTRACT WITNESS confirmed through unchanged NAND-core parser: 10 address cycles accepted, only 8 emitted");
#endif
    reset_fixture(2); addr.type=99;
    assert(sunxi_nfc_exec_op(&chip.nand,&op,true)==-ENOTSUPP && !hw.writes);
    printf("actual NAND-core parser + exec_op: %u read/write/check-only/RB/error cases\n",runs);
}
static u32 oracle_period(const struct nand_sdr_timings *t,const struct sunxi_nfc_timings *c)
{
    const u32 v[]={t->tCLS_min,t->tCLH_min,t->tCS_min,t->tCH_min,t->tWP_min,t->tWH_min,
        t->tALS_min,t->tDS_min,t->tDH_min,t->tRR_min,t->tALH_min,t->tRP_min,t->tREH_min,
        t->tRC_min,t->tWC_min,t->tWB_max,t->tADL_min,t->tWHR_min,t->tRHW_min};
    const u32 d[]={c->setup_cycles,c->setup_cycles,c->setup_cycles,c->setup_cycles,1,1,
        c->setup_cycles,1,1,3,c->setup_cycles,1,1,2,2,c->tWB[3],c->tADL[3],c->tWHR[3],c->tRHW[3]};
    u32 p=1;
    for(unsigned i=0;i<ARRAY_SIZE(v);i++) {
        u32 need=v[i]/d[i]+!!(v[i]%d[i]);
        if(need>p) p=need;
    }
    if(!t->tRLOH_min && t->tREA_max>p) p=t->tREA_max;
    return p;
}
static void check_interface(const struct nand_interface_config *c)
{
    unsigned period=oracle_period(&c->sdr,caps.timings);
    unsigned long rate=1000000000UL/(period/1000+!!(period%1000));
    unsigned oldcfg=chip.timing_cfg,oldctl=chip.timing_ctl;
    unsigned long oldrate=chip.clk_rate;
    assert(!sunxi_nfc_setup_interface(&chip.nand,NAND_DATA_IFACE_CHECK_ONLY,c));
    assert(!hw.clock_calls && !hw.writes && chip.clk_rate==oldrate);
    assert(chip.timing_cfg==oldcfg && chip.timing_ctl==oldctl);
    assert(!sunxi_nfc_setup_interface(&chip.nand,0,c));
    assert(chip.clk_rate==rate && hw.requested_rate==rate);
    const s32 *tables[]={caps.timings->tWB,caps.timings->tADL,caps.timings->tWHR,caps.timings->tRHW};
    const unsigned duration[]={c->sdr.tWB_max,c->sdr.tADL_min,c->sdr.tWHR_min,c->sdr.tRHW_min};
    for(unsigned i=0;i<4;i++) {
        unsigned enc=(chip.timing_cfg>>(2*i))&3;
        assert((u64)tables[i][enc]*period>=duration[i]);
        assert(!enc || (u64)tables[i][enc-1]*period<duration[i]);
    }
    unsigned ns=1000000000UL/(hw.rounded_rate ? hw.rounded_rate : rate);
    assert(!!chip.timing_ctl==(ns*2<30 || ns*1000<c->sdr.tREA_max));
}
static void timing_checks(void)
{
    unsigned runs=0;
    for(unsigned backend=0;backend<3;backend+=2) {
        reset_fixture(backend);
        const s32 *tables[]={caps.timings->tWB,caps.timings->tADL,caps.timings->tWHR,caps.timings->tRHW};
        for(unsigned t=0;t<4;t++)
        for(unsigned period=1;period<100;period+=7)
        for(unsigned duration=0;duration<=8000;duration++) {
            int expected=-EINVAL;
            for(unsigned i=0;i<4;i++) if((u64)tables[t][i]*period>=duration) { expected=i; break; }
            assert(_sunxi_nand_lookup_timing(tables[t],4,duration,period)==expected);
            runs++;
        }
    }
    unsigned interfaces=0;
    for(unsigned backend=0;backend<3;backend+=2)
    for(unsigned sample=0;sample<500;sample++)
    for(unsigned rloh=0;rloh<2;rloh++) {
        reset_fixture(backend);
        struct nand_interface_config c={0};
        /* Named fields; no dependency on struct layout or field-array casting. */
        u32 *fields[]={&c.sdr.tCLS_min,&c.sdr.tCLH_min,&c.sdr.tCS_min,&c.sdr.tCH_min,
            &c.sdr.tWP_min,&c.sdr.tWH_min,&c.sdr.tALS_min,&c.sdr.tDS_min,&c.sdr.tDH_min,
            &c.sdr.tRR_min,&c.sdr.tALH_min,&c.sdr.tRP_min,&c.sdr.tREH_min,&c.sdr.tRC_min,
            &c.sdr.tWC_min,&c.sdr.tWB_max,&c.sdr.tADL_min,&c.sdr.tWHR_min,&c.sdr.tRHW_min};
        for(unsigned i=0;i<ARRAY_SIZE(fields);i++)
            *fields[i]=1+((sample*7919+i*104729)%300001);
        c.sdr.tREA_max=10000+sample*71; c.sdr.tRLOH_min=rloh ? 5000 : 0;
        check_interface(&c);
        interfaces++;
    }
    /* Let every timing field dominate independently. Random combinations can
     * leave late maxima completely shadowed by an earlier, larger field. */
    for(unsigned backend=0;backend<3;backend+=2)
    for(unsigned dominant=0;dominant<20;dominant++) {
        reset_fixture(backend);
        struct nand_interface_config c={0};
        u32 *fields[]={&c.sdr.tCLS_min,&c.sdr.tCLH_min,&c.sdr.tCS_min,&c.sdr.tCH_min,
            &c.sdr.tWP_min,&c.sdr.tWH_min,&c.sdr.tALS_min,&c.sdr.tDS_min,&c.sdr.tDH_min,
            &c.sdr.tRR_min,&c.sdr.tALH_min,&c.sdr.tRP_min,&c.sdr.tREH_min,&c.sdr.tRC_min,
            &c.sdr.tWC_min,&c.sdr.tWB_max,&c.sdr.tADL_min,&c.sdr.tWHR_min,&c.sdr.tRHW_min,
            &c.sdr.tREA_max};
        for(unsigned i=0;i<ARRAY_SIZE(fields);i++) *fields[i]=1000;
        *fields[dominant]=300001;
        check_interface(&c);
        interfaces++;
    }
    /* Both EDO conditions, the 30 ns boundary, and a slower rounded clock. */
    for(unsigned backend=0;backend<3;backend+=2)
    for(unsigned wp=10000;wp<=16000;wp+=1000)
    for(unsigned rea=10000;rea<=20000;rea+=10000)
    for(unsigned rounded=0;rounded<2;rounded++) {
        reset_fixture(backend);
        struct nand_interface_config c={.sdr={.tWP_min=wp,.tREA_max=rea,.tRLOH_min=5000}};
        hw.rounded_rate=rounded ? 50000000 : 0;
        check_interface(&c);
        interfaces++;
    }
    reset_fixture(2);
    struct nand_interface_config c={.sdr={.tWP_min=20000,.tWB_max=200000,.tREA_max=20000}};
    hw.rounded_rate=-EIO;
    assert(sunxi_nfc_setup_interface(&chip.nand,0,&c)==-EINVAL);
    c.invalid=true; assert(sunxi_nfc_setup_interface(&chip.nand,0,&c)==-ENOTSUPP);
    printf("timing oracle: %u lookup cases; %u complete interface/EDO/check-only cases\n",runs,interfaces);
}
static void selection_checks(void)
{
    reset_fixture(2);
    struct sunxi_nand_chip other=chip;
    other.nand.mtd.writesize=4096; other.nand.page_shift=12;
    other.clk_rate=48000000; other.timing_cfg=0x634; other.timing_ctl=0;
    for(unsigned i=0;i<10;i++) {
        struct sunxi_nand_chip *p=i%2 ? &other : &chip;
        sunxi_nfc_select_chip(&p->nand,i%2);
        assert(load(caps.reg_spare_area)==p->nand.mtd.writesize);
        assert(load(NFC_REG_TIMING_CFG)==p->timing_cfg && load(NFC_REG_TIMING_CTL)==p->timing_ctl);
        assert(((load(NFC_REG_CTL)>>24)&7)==p->sels[i%2].cs);
        assert(hw.real_rate==p->clk_rate && nfc.clk_rate==p->clk_rate);
    }
    unsigned writes=hw.writes;
    sunxi_nfc_select_chip(&chip.nand,2); assert(hw.writes==writes);
    chip.sels[0].rb=-1; sunxi_nfc_select_chip(&chip.nand,0);
    assert(!(load(NFC_REG_CTL)&NFC_RB_SEL_MSK));
#if FIXED_SUNXI
    unsigned oldwrites=hw.writes, calls=hw.clock_calls;
    hw.clock_set_error=-EBUSY;
    assert(sunxi_nfc_select_chip(&other.nand,0)==-EBUSY);
    assert(nfc.clk_rate==chip.clk_rate && hw.real_rate==chip.clk_rate);
    assert(hw.writes==oldwrites);
    assert(sunxi_nfc_select_chip(&other.nand,0)==-EBUSY);
    assert(hw.clock_calls==calls+2 && hw.writes==oldwrites);
    struct nand_op_instr cmd={.type=NAND_OP_CMD_INSTR,.ctx.cmd.opcode=0x70};
    struct nand_operation op={.instrs=&cmd,.ninstrs=1};
    assert(!sunxi_nfc_exec_op(&other.nand,&op,true));
    assert(hw.clock_calls==calls+2 && hw.writes==oldwrites);
    assert(sunxi_nfc_exec_op(&other.nand,&op,false)==-EBUSY);
    assert(!hw.commands && hw.writes==oldwrites);
    hw.clock_set_error=0;
    assert(!sunxi_nfc_exec_op(&other.nand,&op,false));
    assert(hw.commands==1 && hw.real_rate==other.clk_rate && nfc.clk_rate==other.clk_rate);
    puts("FIX VERIFIED: failed clock setup leaves registers/cache unchanged, propagates through exec_op, and retries successfully");
#else
    hw.clock_set_error=-EIO; sunxi_nfc_select_chip(&other.nand,0);
    assert(nfc.clk_rate==other.clk_rate && hw.real_rate!=other.clk_rate);
    unsigned calls=hw.clock_calls;
    sunxi_nfc_select_chip(&other.nand,0); assert(hw.clock_calls==calls);
    puts("chip context alternation passes; SOURCE CONTRACT WITNESS: failed clk_set_rate leaves cached/requested and physical clocks different");
#endif
}

static void selection_matrix(void)
{
#if FIXED_SUNXI
    unsigned runs=0;
    for(unsigned backend=0;backend<3;backend++)
    for(unsigned same_rate=0;same_rate<2;same_rate++)
    for(unsigned error=0;error<2;error++)
    for(unsigned sel=0;sel<2;sel++) {
        reset_fixture(backend);
        struct sunxi_nand_chip other=chip;
        other.nand.mtd.writesize=4096; other.nand.page_shift=12;
        other.clk_rate=same_rate ? chip.clk_rate : 48000000;
        other.timing_cfg=0x634; other.timing_ctl=0;
        assert(!sunxi_nfc_select_chip(&chip.nand,0));
        u8 saved[sizeof(registers)]; memcpy(saved,registers,sizeof(saved));
        unsigned calls=hw.clock_calls,writes=hw.writes;
        hw.clock_set_error=error ? -EINVAL : -EBUSY;
        struct nand_op_instr cmd={.type=NAND_OP_CMD_INSTR,.ctx.cmd.opcode=0x70};
        struct nand_operation op={.cs=sel,.instrs=&cmd,.ninstrs=1};
        assert(!sunxi_nfc_exec_op(&other.nand,&op,true));
        assert(hw.clock_calls==calls && hw.writes==writes);
        assert(!memcmp(saved,registers,sizeof(saved)));
        if(!same_rate) {
            for(unsigned retry=0;retry<3;retry++) {
                assert(sunxi_nfc_exec_op(&other.nand,&op,false)==hw.clock_set_error);
                assert(hw.clock_calls==calls+retry+1 && !hw.commands);
                assert(nfc.clk_rate==chip.clk_rate && hw.real_rate==chip.clk_rate);
                assert(hw.writes==writes && !memcmp(saved,registers,sizeof(saved)));
            }
        }
        hw.clock_set_error=0;
        assert(!sunxi_nfc_exec_op(&other.nand,&op,false));
        assert(hw.commands==1 && nfc.clk_rate==other.clk_rate);
        assert(load(caps.reg_spare_area)==4096);
        assert(load(NFC_REG_TIMING_CFG)==other.timing_cfg && load(NFC_REG_TIMING_CTL)==0);
        assert(((load(NFC_REG_CTL)>>24)&7)==other.sels[sel].cs);
        assert(!!(load(NFC_REG_CTL)&NFC_RB_SEL_MSK)==!!other.sels[sel].rb);
        if(same_rate) assert(hw.clock_calls==calls);
        assert(!sunxi_nfc_select_chip(&chip.nand,0));
        assert(load(caps.reg_spare_area)==2048 && load(NFC_REG_TIMING_CFG)==chip.timing_cfg);
        runs++;
    }
    printf("three-backend equal/different-clock chip-switch/error/retry matrix: %u\n",runs);
#endif
}
int main(void)
{
    dma_checks(); dma_init_checks(); irq_checks(); raw_media_checks(); command_checks(); parser_checks(); timing_checks(); selection_checks();
    selection_matrix();
    puts("ALL LOWLEVEL CHECKS PASSED"); return 0;
}
