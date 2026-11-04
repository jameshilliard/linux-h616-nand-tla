/* SPDX-License-Identifier: GPL-2.0-or-later */
enum { PAGE=2048, SPARE=256, PPB=4, BLOCKS=64, TARGETS=2, PAGES=BLOCKS*PPB*TARGETS };
extern void driver_reset(unsigned,bool),driver_faults(int,int),driver_finish(void);
extern void driver_erase(unsigned,unsigned);
extern int driver_region(int,unsigned *,unsigned *);
extern int driver_ecc_region(int,unsigned *,unsigned *);
extern int driver_io(bool,bool,bool,unsigned,unsigned,void *,void *,bool,unsigned *,unsigned *);
static struct nand_chip chip;
static bool use_dma;
static u8 bounce[PAGE],oobbuf[SPARE],table[BLOCKS*TARGETS/4];
static unsigned requests,callbacks;
static int free_region(struct mtd_info *m,int s,struct mtd_oob_region *r)
{ return driver_region(s,&r->offset,&r->length); }
static const struct mtd_ooblayout_ops layout={.free=free_region};
static int io(struct nand_chip *c,bool wr,bool raw,void *b,int o,int p)
{
    unsigned corrected,failed;
    assert(c->selected>=0 && c->selected<TARGETS);
    int ret=driver_io(wr,use_dma,raw,c->selected,p,b,c->oob_poi,o,&corrected,&failed);
    c->mtd.ecc_stats.corrected+=corrected; c->mtd.ecc_stats.failed+=failed;
    callbacks++; return ret;
}
static int read_page(struct nand_chip *c,u8 *b,int o,int p) { return io(c,false,false,b,o,p); }
static int read_raw(struct nand_chip *c,u8 *b,int o,int p) { return io(c,false,true,b,o,p); }
static int read_sub(struct nand_chip *c,u32 off,u32 len,u8 *b,int p) { return io(c,false,false,b,0,p); }
static int read_oob(struct nand_chip *c,int p) { return io(c,false,false,NULL,1,p); }
static int read_oob_raw(struct nand_chip *c,int p) { return io(c,false,true,NULL,1,p); }
static int write_page(struct nand_chip *c,const u8 *b,int o,int p) { return io(c,true,false,(void *)b,o,p); }
static int write_raw(struct nand_chip *c,const u8 *b,int o,int p) { return io(c,true,true,(void *)b,o,p); }
static int write_oob(struct nand_chip *c,int p) { return io(c,true,false,NULL,1,p); }
static int write_oob_raw(struct nand_chip *c,int p) { return io(c,true,true,NULL,1,p); }
static int mtd_read(struct mtd_info *m,loff_t from,size_t len,size_t *retlen,u8 *buf)
{
    struct mtd_oob_ops op={.mode=MTD_OPS_PLACE_OOB,.len=len,.datbuf=buf};
    int ret=nand_do_read_ops(&chip,from,&op); *retlen=op.retlen; return ret>0?-EUCLEAN:ret;
}
static int mtd_read_oob(struct mtd_info *m,loff_t from,struct mtd_oob_ops *o)
{ int ret=o->datbuf?nand_do_read_ops(&chip,from,o):nand_do_read_oob(&chip,from,o); return ret>0?-EUCLEAN:ret; }
static int mtd_write_oob(struct mtd_info *m,loff_t from,struct mtd_oob_ops *o)
{ return o->datbuf?nand_do_write_ops(&chip,from,o):nand_do_write_oob(&chip,from,o); }
static int nand_erase_nand(struct nand_chip *c,struct erase_info *e,int allow)
{ assert(e->addr%(PPB*PAGE)==0 && e->len==PPB*PAGE); driver_erase(e->addr/PAGE,PPB); return 0; }
static void setup(unsigned format,bool variable)
{
    memset(&chip,0,sizeof(chip)); memset(table,0,sizeof(table)); driver_reset(format,variable);
    chip.mtd=(struct mtd_info){.writesize=PAGE,.oobsize=SPARE,.erasesize=PPB*PAGE,
        .size=(loff_t)PAGE*PAGES,.ooblayout=&layout};
    struct mtd_oob_region r;
    for(int s=0;!free_region(&chip.mtd,s,&r);s++) chip.mtd.oobavail+=r.length;
    chip.base=(struct nand_device){.targetsize=BLOCKS*PPB*PAGE,.targets=TARGETS};
    chip.page_shift=11; chip.chip_shift=19; chip.bbt_erase_shift=chip.phys_erase_shift=13;
    chip.pagemask=BLOCKS*PPB-1; chip.buf_align=4; chip.subpagesize=PAGE;
    chip.pagecache.page=-1; chip.selected=-1; chip.data_buf=bounce; chip.oob_poi=oobbuf; chip.bbt=table;
    chip.options=NAND_NO_SUBPAGE_WRITE;
    chip.ecc.read_page=read_page; chip.ecc.read_page_raw=read_raw; chip.ecc.read_subpage=read_sub;
    chip.ecc.read_oob=read_oob; chip.ecc.read_oob_raw=read_oob_raw;
    chip.ecc.write_page=write_page; chip.ecc.write_page_raw=write_raw;
    chip.ecc.write_oob=write_oob; chip.ecc.write_oob_raw=write_oob_raw;
}
int main(void)
{
    u8 data[3*PAGE],out[3*PAGE+2],metadata[3*SPARE],got[3*SPARE+2];
    unsigned scenarios=0;
    for(unsigned fmt=0;fmt<3;fmt++) for(unsigned variable=0;variable<2;variable++)
    for(unsigned writer=0;writer<2;writer++) for(unsigned reader=0;reader<2;reader++) {
        setup(fmt,variable); use_dma=writer;
        unsigned start=BLOCKS*PPB-1,freebytes=chip.mtd.oobavail;
        for(unsigned i=0;i<sizeof(data);i++) data[i]=(i*19+(i>>8)*7+fmt);
        for(unsigned i=0;i<sizeof(metadata);i++) metadata[i]=i*13+37;
        struct mtd_oob_ops op={.mode=MTD_OPS_AUTO_OOB,.len=sizeof(data),.datbuf=data,
            .ooblen=3*freebytes,.oobbuf=metadata};
        assert(!nand_do_write_ops(&chip,(loff_t)start*PAGE,&op));
        assert(op.retlen==sizeof(data) && op.oobretlen==3*freebytes);
        use_dma=reader; memset(out,0xa5,sizeof(out)); memset(got,0xa5,sizeof(got));
        op=(struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB,.len=sizeof(data)-31,.datbuf=out+1,
            .ooblen=3*freebytes,.oobbuf=got+1};
        assert(!nand_do_read_ops(&chip,(loff_t)start*PAGE+17,&op));
        assert(op.retlen==sizeof(data)-31 && !memcmp(out+1,data+17,op.retlen));
        assert(op.oobretlen==3*freebytes && !memcmp(got+1,metadata,op.oobretlen));
        assert(out[0]==0xa5 && got[0]==0xa5 && got[1+op.oobretlen]==0xa5);
        requests+=2;
        op=(struct mtd_oob_ops){.mode=MTD_OPS_PLACE_OOB,.len=PAGE,.datbuf=out,
            .ooblen=SPARE,.oobbuf=got};
        assert(!nand_do_read_ops(&chip,(loff_t)start*PAGE,&op));
        if(reader || !fmt) {
            unsigned off,len;
            for(int s=0;!driver_ecc_region(s,&off,&len);s++)
                for(unsigned i=off;i<off+len;i++) assert(got[i]==0x5b);
        }
        requests++;
        /* RAW -> OOB-only -> corrected reads, without resetting registers. */
        op=(struct mtd_oob_ops){.mode=MTD_OPS_RAW,.len=PAGE,.datbuf=out};
        assert(!nand_do_read_ops(&chip,(loff_t)start*PAGE,&op));
        op=(struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB,.ooblen=freebytes,.oobbuf=got};
        assert(!nand_do_read_oob(&chip,(loff_t)start*PAGE,&op));
        assert(!memcmp(got,metadata,freebytes));
        driver_faults(0,-1);
        op=(struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB,.len=sizeof(data),.datbuf=out};
        assert(nand_do_read_ops(&chip,(loff_t)start*PAGE,&op)==-EIO && op.retlen==PAGE);
        driver_faults(-1,-1);
        op=(struct mtd_oob_ops){.mode=MTD_OPS_PLACE_OOB,.len=sizeof(data),.datbuf=out};
        assert(!nand_do_read_ops(&chip,(loff_t)start*PAGE,&op) && !memcmp(out,data,sizeof(data)));
        requests+=4;
        /* ECC failure must propagate through core counters, not look clean. */
        driver_faults(-1,1); chip.pagecache.page=-1;
        op=(struct mtd_oob_ops){.mode=MTD_OPS_PLACE_OOB,.len=PAGE,.datbuf=out};
        assert(nand_do_read_ops(&chip,(loff_t)(start+2)*PAGE,&op)==-EBADMSG);
        assert(chip.mtd.ecc_stats.failed); driver_faults(-1,-1); requests++;
        driver_finish(); scenarios++;
    }
    /* Real flash-BBT scans/writes/reloads through the same page callbacks. */
    for(unsigned fmt=0;fmt<3;fmt++) for(unsigned dma=0;dma<2;dma++) {
        setup(fmt,true); use_dma=dma;
        u8 good[]={255},pat[]={'B','b','t','0'},mir[]={'1','t','b','B'};
        struct nand_bbt_descr bd={.offs=0,.len=1,.pattern=good};
        struct nand_bbt_descr td={.options=NAND_BBT_2BIT|NAND_BBT_VERSION|NAND_BBT_CREATE|
            NAND_BBT_WRITE|NAND_BBT_LASTBLOCK|NAND_BBT_NO_OOB,.offs=0,.veroffs=4,.len=4,.maxblocks=4,.pattern=pat};
        struct nand_bbt_descr md=td; md.pattern=mir;
        for(unsigned i=0;i<NAND_MAX_CHIPS;i++) td.pages[i]=md.pages[i]=-1;
        chip.bbt_td=&td; chip.bbt_md=&md; chip.badblock_pattern=&bd; chip.bbt_options=NAND_BBT_NO_OOB|NAND_BBT_USE_FLASH;
        u8 bbtbuf[PPB*(PAGE+SPARE)];
        assert(!check_create(&chip,bbtbuf,&bd));
        assert(td.pages[0]>=0 && md.pages[0]>=0 && td.pages[0]!=md.pages[0]);
        assert(!nand_markbad_bbt(&chip,3*chip.mtd.erasesize));
        memset(table,0,sizeof(table));
        assert(!read_abs_bbt(&chip,table,&td,-1));
        assert(nand_isbad_bbt(&chip,3*chip.mtd.erasesize,0));
        driver_finish(); scenarios++;
    }
    printf("Composed core/driver: %u profiles; %u requests; %u page/OOB adapter calls\n",scenarios,requests,callbacks);
    puts("ALL CHECKS PASSED");
}
