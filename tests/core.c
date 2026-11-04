/* SPDX-License-Identifier: GPL-2.0-or-later */
enum { PAGE = 1024, SPARE = 64, PPB = 4, BLOCKS = 64, TARGETS = 2,
       PAGES = BLOCKS * PPB * TARGETS };
static struct nand_chip chip;
static u8 media[PAGES][PAGE], spare[PAGES][SPARE];
static u8 bounce[PAGE], oobbuf[SPARE], ram_bbt[BLOCKS * TARGETS / 4];
static int page_error[PAGES], main_error[PAGES], retry_mode, retry_fail_page;
static int erase_fail_block, program_fail_page, retry_setup_error, retry_reset_error;
static bool compound_program_fail[PAGES];
static bool compound_erase_fail[BLOCKS*TARGETS];
static unsigned int reads, raw_reads, subreads, writes, raw_writes, subwrites, retries;
static unsigned int scanned, scan_modes, programs[PAGES];
static u8 good_pattern[] = {255, 255}, primary_pattern[] = {'B','b','t','0'};
static u8 mirror_pattern[] = {'1','t','b','B'};
static struct nand_bbt_descr bad, primary, mirror;
static unsigned int checks;
#define CHECK(e) do { checks++; if (!(e)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

static int free_region(struct mtd_info *m, int section, struct mtd_oob_region *r)
{
    /* Two fragmented regions, with BBM and parity deliberately excluded. */
    if (section == 0) { r->offset = 2; r->length = 2; return 0; }
    if (section == 1) { r->offset = 32; r->length = 4; return 0; }
    return -ERANGE;
}
static const struct mtd_ooblayout_ops layout = { .free = free_region };
static int global_page(struct nand_chip *c, int p)
{
    CHECK(c->selected >= 0 && c->selected < TARGETS && p >= 0 && p < BLOCKS * PPB);
    return c->selected * BLOCKS * PPB + p;
}
static int read_page_common(struct nand_chip *c, u8 *buf, int need_oob, int p, bool raw)
{
    int g = global_page(c, p), err = page_error[g];
    reads++; raw_reads += raw;
    if (err && !mtd_is_bitflip_or_eccerr(err)) return err;
    memcpy(buf, media[g], PAGE);
    if (need_oob) memcpy(c->oob_poi, spare[g], SPARE);
    if (!raw && (err == -EBADMSG || (g == retry_fail_page && !retry_mode))) {
        c->mtd.ecc_stats.failed++;
        /* A failed first attempt must not leak this marker into final OOB. */
        if (need_oob && g == retry_fail_page) c->oob_poi[2] ^= 0x80;
        return 0;
    }
    if (!raw && err == -EUCLEAN) { c->mtd.ecc_stats.corrected += 3; return 3; }
    return 0;
}
static int read_page(struct nand_chip *c, u8 *b, int o, int p) { return read_page_common(c,b,o,p,false); }
static int read_raw(struct nand_chip *c, u8 *b, int o, int p) { return read_page_common(c,b,o,p,true); }
static int read_sub(struct nand_chip *c, u32 col, u32 n, u8 *b, int p)
{ subreads++; return read_page(c,b,0,p); }
static int read_oob(struct nand_chip *c, int p)
{ u8 b[PAGE]; return read_page(c,b,1,p); }
static int read_oob_raw(struct nand_chip *c, int p)
{ u8 b[PAGE]; return read_raw(c,b,1,p); }
static int setup_retry(struct nand_chip *c, int mode)
{
    retries++;
    if (mode && retry_setup_error) return retry_setup_error;
    if (!mode && retry_reset_error) return retry_reset_error;
    retry_mode = mode;
    return 0;
}
static int program(struct nand_chip *c, const u8 *b, int need_oob, int p, bool raw)
{
    int g = global_page(c,p);
    writes++; raw_writes += raw;
    if (g == program_fail_page || compound_program_fail[g]) return -EIO;
    programs[g]++;
    if (b) for (int i = 0; i < PAGE; i++) media[g][i] &= b[i];
    if (need_oob) for (int i = 0; i < SPARE; i++) spare[g][i] &= c->oob_poi[i];
    return 0;
}
static int write_page(struct nand_chip *c, const u8 *b, int o, int p) { return program(c,b,o,p,false); }
static int write_raw(struct nand_chip *c, const u8 *b, int o, int p) { return program(c,b,o,p,true); }
static int write_sub(struct nand_chip *c, u32 col, u32 n, const u8 *b, int o, int p)
{ subwrites++; return program(c,b,o,p,false); }
static int write_oob(struct nand_chip *c, int p) { return program(c,NULL,1,p,false); }
static int write_oob_raw(struct nand_chip *c, int p) { return program(c,NULL,1,p,true); }
static int mtd_read(struct mtd_info *m, loff_t from, size_t n, size_t *retlen, u8 *b)
{
    int g = from / PAGE, e = main_error[g];
    CHECK(from >= 0 && from + n <= sizeof(media));
    if (e && !mtd_is_bitflip_or_eccerr(e)) { *retlen = 0; return e; }
    memcpy(b, (u8 *)media + from, n); *retlen = n; return e;
}
static int mtd_read_oob(struct mtd_info *m, loff_t from, struct mtd_oob_ops *ops)
{
    struct nand_chip *c = mtd_to_nand(m);
    int ret;
    scanned++; scan_modes |= BIT(ops->mode);
    CHECK(from >= 0 && from < sizeof(media));
    ret = ops->datbuf ? nand_do_read_ops(c,from,ops) : nand_do_read_oob(c,from,ops);
    return ret > 0 ? -EUCLEAN : ret;
}
static int mtd_write_oob(struct mtd_info *m, loff_t to, struct mtd_oob_ops *ops)
{
    return ops->datbuf ? nand_do_write_ops(mtd_to_nand(m),to,ops) :
                        nand_do_write_oob(mtd_to_nand(m),to,ops);
}
static int nand_erase_nand(struct nand_chip *c, struct erase_info *e, int allowbbt)
{
    unsigned int b = e->addr / (PPB * PAGE);
    CHECK(e->addr % (PPB * PAGE) == 0 && e->len == PPB * PAGE && b < BLOCKS * TARGETS);
    if ((int)b == erase_fail_block || compound_erase_fail[b]) return -EIO;
    memset(media[b * PPB],255,PPB * PAGE);
    memset(spare[b * PPB],255,PPB * SPARE);
    memset(programs + b * PPB,0,PPB * sizeof(*programs));
    return 0;
}
static void reset(bool patterned)
{
    memset(&chip,0,sizeof(chip)); memset(media,255,sizeof(media)); memset(spare,255,sizeof(spare));
    memset(ram_bbt,0,sizeof(ram_bbt)); memset(page_error,0,sizeof(page_error));
    memset(main_error,0,sizeof(main_error)); memset(programs,0,sizeof(programs));
    memset(compound_program_fail,0,sizeof(compound_program_fail));
    memset(compound_erase_fail,0,sizeof(compound_erase_fail));
    chip.mtd = (struct mtd_info){.writesize=PAGE,.oobsize=SPARE,.oobavail=6,
        .erasesize=PPB*PAGE,.size=sizeof(media),.ooblayout=&layout};
    chip.base = (struct nand_device){.targetsize=BLOCKS*PPB*PAGE,.targets=TARGETS};
    chip.page_shift=10; chip.chip_shift=18; chip.bbt_erase_shift=chip.phys_erase_shift=12;
    chip.pagemask=BLOCKS*PPB-1; chip.buf_align=32; chip.subpagesize=256;
    chip.pagecache.page=-1; chip.selected=-1; chip.data_buf=bounce; chip.oob_poi=oobbuf;
    chip.bbt=ram_bbt; chip.read_retries=2; chip.ops.setup_read_retry=setup_retry;
    chip.ecc.read_page=read_page; chip.ecc.read_page_raw=read_raw; chip.ecc.read_subpage=read_sub;
    chip.ecc.read_oob=read_oob; chip.ecc.read_oob_raw=read_oob_raw;
    chip.ecc.write_page=write_page; chip.ecc.write_page_raw=write_raw; chip.ecc.write_subpage=write_sub;
    chip.ecc.write_oob=write_oob; chip.ecc.write_oob_raw=write_oob_raw;
    bad=(struct nand_bbt_descr){.offs=0,.len=1,.pattern=good_pattern};
    primary=(struct nand_bbt_descr){.options=NAND_BBT_2BIT|NAND_BBT_VERSION|NAND_BBT_CREATE|
        NAND_BBT_WRITE|NAND_BBT_LASTBLOCK,.offs=8,.veroffs=12,.len=4,.maxblocks=4,.pattern=primary_pattern};
    mirror=primary; mirror.pattern=mirror_pattern;
    for (int t=0;t<NAND_MAX_CHIPS;t++) primary.pages[t]=mirror.pages[t]=-1;
    chip.bbt_td=&primary; chip.bbt_md=&mirror; chip.badblock_pattern=&bad;
    retry_mode=0; retry_fail_page=erase_fail_block=program_fail_page=-1;
    retry_setup_error=retry_reset_error=reset_error=0; secure_region=write_protected=device_locked=false;
    reads=raw_reads=subreads=writes=raw_writes=subwrites=retries=scanned=scan_modes=0;
    if (patterned) for (int p=0;p<PAGES;p++) {
        for (int i=0;i<PAGE;i++) media[p][i]=(p*71+i*13) ^ (i>>3);
        for (int i=0;i<SPARE;i++) spare[p][i]=p*11+i*3;
    }
}
static void request_checks(void)
{
    u8 data[PAGE*3+32], oob[SPARE*3+32], expected[SPARE*3];
    for (unsigned int mode=0;mode<3;mode++) for (int partial=0;partial<2;partial++)
    for (int dma=0;dma<2;dma++) for (int with_oob=0;with_oob<2;with_oob++) {
        reset(true); chip.options=dma?NAND_USES_DMA:0;
        int col=partial?17:0, start=BLOCKS*PPB-1;
        size_t n=PAGE*2-col-11, on=mode==MTD_OPS_AUTO_OOB?12:SPARE*2;
        struct mtd_oob_ops ops={.mode=mode,.len=n,.datbuf=data+1,
            .oobbuf=with_oob?oob:NULL,.ooblen=with_oob?on:0};
        CHECK(nand_do_read_ops(&chip,(loff_t)start*PAGE+col,&ops)==0);
        CHECK(ops.retlen==n && !memcmp(data+1,(u8 *)media+start*PAGE+col,n));
        CHECK(chip.selected==-1);
        if (with_oob) {
            for (int j=0;j<2;j++) {
                if (mode==MTD_OPS_AUTO_OOB) CHECK(mtd_ooblayout_get_databytes(&chip.mtd,expected+j*6,spare[start+j],0,6)==0);
                else memcpy(expected+j*SPARE,spare[start+j],SPARE);
            }
            CHECK(ops.oobretlen==on && !memcmp(oob,expected,on));
        }
        CHECK((mode==MTD_OPS_RAW)==(raw_reads>0));
    }
    for (int mode=0;mode<3;mode++) {
        reset(true);
        struct mtd_oob_ops ops={.mode=mode,.ooblen=2,.ooboffs=1,.oobbuf=oob};
        CHECK(nand_do_read_oob(&chip,0,&ops)==0 && ops.oobretlen==2);
        CHECK(oob[0]==spare[0][mode==MTD_OPS_AUTO_OOB?3:1]);
    }
    reset(true);
    struct mtd_oob_ops ops={.mode=MTD_OPS_PLACE_OOB,.len=19,.datbuf=data};
    CHECK(nand_do_read_ops(&chip,7,&ops)==0 && chip.pagecache.page==0);
    unsigned int oldreads=reads;
    CHECK(nand_do_read_ops(&chip,11,&ops)==0 && reads==oldreads);
    chip.pagecache.page=-1; chip.subpage_read=true;
    CHECK(nand_do_read_ops(&chip,11,&ops)==0 && subreads==1 && chip.pagecache.page==-1);
    reset(true); retry_fail_page=0; ops=(struct mtd_oob_ops){.len=2*PAGE,.datbuf=data};
    CHECK(nand_do_read_ops(&chip,0,&ops)==0 && retries==2 && retry_mode==0);
    CHECK(chip.mtd.ecc_stats.failed==0 && !memcmp(data,media,2*PAGE));
    for (int fault=0;fault<4;fault++) {
        reset(true); ops=(struct mtd_oob_ops){.len=2*PAGE,.datbuf=data};
        page_error[1]=fault==0?-EIO:fault==1?-EBADMSG:-EUCLEAN;
        if (fault==3) { retry_fail_page=0; retry_setup_error=-EIO; }
        int ret=nand_do_read_ops(&chip,0,&ops);
        CHECK(ret==(fault==0||fault==3?-EIO:fault==1?-EBADMSG:3));
        CHECK(ops.retlen==(fault==3?0:fault==0?PAGE:2*PAGE));
    }
    /* Known source-order witness: failed attempt OOB is published before retry. */
    for (int mode=0;mode<2;mode++) {
        reset(true); retry_fail_page=0;
        unsigned int per=mode==MTD_OPS_AUTO_OOB?6:SPARE;
        ops=(struct mtd_oob_ops){.mode=mode,.len=2*PAGE,.datbuf=data,.oobbuf=oob,.ooblen=2*per};
        CHECK(nand_do_read_ops(&chip,0,&ops)==0 && ops.retlen==2*PAGE && ops.oobretlen==2*per);
        if (mode==MTD_OPS_AUTO_OOB) CHECK(mtd_ooblayout_get_databytes(&chip.mtd,expected,spare[1],0,per)==0);
        else memcpy(expected,spare[1],per);
        CHECK(memcmp(oob+per,expected,per)!=0 && reads==3 && chip.mtd.ecc_stats.failed==0);
    }
    puts("SOURCE WITNESS: successful read retry duplicates page-0 OOB into page-1 output");
    for (int mode=0;mode<3;mode++) for (int partial=0;partial<2;partial++) {
        reset(false); memset(data,0x5a,sizeof(data)); memset(oob,0xa5,sizeof(oob));
        unsigned int n=partial?256:2*PAGE, col=partial?256:0, on=mode==MTD_OPS_AUTO_OOB?6:SPARE;
        ops=(struct mtd_oob_ops){.mode=mode,.len=n,.datbuf=data+1,.oobbuf=oob,.ooblen=on};
        chip.options=NAND_USES_DMA; chip.pagecache.page=0;
        CHECK(nand_do_write_ops(&chip,col,&ops)==0 && ops.retlen==n);
        /* The source invalidates only when the cached page start is in range. */
        CHECK(chip.pagecache.page==(partial?0:-1));
        for (unsigned int i=0;i<PAGE;i++) CHECK(media[0][i]==(i>=col && i<col+n?0x5a:0xff));
        CHECK((mode==MTD_OPS_RAW)==(raw_writes>0));
    }
    reset(false); ops=(struct mtd_oob_ops){.len=2*PAGE,.datbuf=data,.oobbuf=oob,.ooblen=2*SPARE};
    chip.pagecache.page=0; program_fail_page=1;
    CHECK(nand_do_write_ops(&chip,0,&ops)==-EIO && ops.retlen==PAGE && ops.oobretlen==2*SPARE);
    puts("requests: AUTO/PLACE/RAW, bounce, cache, partial, multi-target, stats and retry errors checked");
}

static void bbt_checks(void)
{
    u8 buf[PPB*(PAGE+SPARE)];
    for (unsigned int flags=0;flags<8;flags++) for (int width=0;width<2;width++) {
        reset(false); chip.options=(flags&1?NAND_BBM_FIRSTPAGE:0)|(flags&2?NAND_BBM_SECONDPAGE:0)|
            (flags&4?NAND_BBM_LASTPAGE:0)|(width?NAND_BUSWIDTH_16:0);
        bad.len=width?2:1;
        CHECK(nand_markbad_bbm(&chip,PPB*PAGE)==0);
        for (int p=0;p<PPB;p++) {
            bool marked=(flags==0?p==0:((flags&1)&&p==0)||((flags&2)&&p==1)||((flags&4)&&p==PPB-1));
            CHECK(spare[PPB+p][0]==(marked?0:255));
            if (width) CHECK(spare[PPB+p][1]==(marked?0:255));
        }
        page_error[PPB]=-EBADMSG;
        CHECK(scan_block_fast(&chip,&bad,PPB*PAGE,buf)==1);
        CHECK(create_bbt(&chip,buf,&bad,-1)==0 && bbt_get_entry(&chip,1)==BBT_BLOCK_FACTORY_BAD);
        CHECK(scan_modes==BIT(MTD_OPS_PLACE_OOB));
    }
    for (int no_oob=0;no_oob<2;no_oob++) for (int perchip=0;perchip<2;perchip++) {
        reset(false);
        if (no_oob) { primary.options|=NAND_BBT_NO_OOB; primary.veroffs=4; primary.offs=0; }
        if (perchip) primary.options|=NAND_BBT_PERCHIP;
        mirror=primary; mirror.pattern=mirror_pattern;
        CHECK(nand_markbad_bbm(&chip,PPB*PAGE)==0);
        CHECK(nand_markbad_bbm(&chip,(BLOCKS+2)*PPB*PAGE)==0);
        CHECK(check_create(&chip,buf,&bad)==0);
        CHECK(bbt_get_entry(&chip,1)==3 && bbt_get_entry(&chip,BLOCKS+2)==3);
        for (int t=0;t<(perchip?2:1);t++) CHECK(primary.pages[t]!=mirror.pages[t] && primary.pages[t]>=0);
        bbt_mark_entry(&chip,3,BBT_BLOCK_WORN);
        CHECK(nand_update_bbt(&chip,0)==0 && primary.version[0]==2);
        memset(ram_bbt,0,sizeof(ram_bbt));
        search_read_bbts(&chip,buf,&primary,&mirror);
        CHECK(check_create(&chip,buf,&bad)==0);
        CHECK(bbt_get_entry(&chip,1)==3 && bbt_get_entry(&chip,3)==1 && bbt_get_entry(&chip,BLOCKS+2)==3);
    }
    for (int no_marker=0;no_marker<2;no_marker++) for (int fail=0;fail<2;fail++) {
        reset(false); chip.bbt_options=NAND_BBT_USE_FLASH|(no_marker?NAND_BBT_NO_OOB_BBM:0);
        CHECK(check_create(&chip,buf,&bad)==0);
        program_fail_page=fail?5*PPB:-1;
        CHECK(nand_block_markbad_lowlevel(&chip,5*PPB*PAGE)==(fail && !no_marker?-EIO:0));
        CHECK(!device_locked && nand_isbad_bbt(&chip,5*PPB*PAGE,0)==1);
        CHECK(spare[5*PPB][0]==(no_marker||fail?255:0));
        memset(ram_bbt,0,sizeof(ram_bbt)); search_read_bbts(&chip,buf,&primary,&mirror);
        CHECK(check_create(&chip,buf,&bad)==0 && bbt_get_entry(&chip,5)==BBT_BLOCK_WORN);
        mark_bbt_region(&chip,&primary);
        CHECK(nand_isbad_bbt(&chip,(loff_t)primary.pages[0]*PAGE,0)==1);
        CHECK(nand_isbad_bbt(&chip,(loff_t)primary.pages[0]*PAGE,1)==0);
        CHECK(nand_isbad_bbt(&chip,0,0)==0);
    }
    /* Bad BBT destination: preserve mirror separation and retry another block. */
    reset(false); erase_fail_block=BLOCKS*TARGETS-1;
    CHECK(check_create(&chip,buf,&bad)==0);
    CHECK(primary.pages[0]/PPB!=erase_fail_block && mirror.pages[0]!=primary.pages[0]);
    CHECK(bbt_get_entry(&chip,erase_fail_block)!=BBT_BLOCK_GOOD);
    reset(false); program_fail_page=(BLOCKS*TARGETS-1)*PPB;
    CHECK(check_create(&chip,buf,&bad)==0 && primary.pages[0]!=program_fail_page);
    reset(false); for (int b=BLOCKS*TARGETS-4;b<BLOCKS*TARGETS;b++) bbt_mark_entry(&chip,b,BBT_BLOCK_FACTORY_BAD);
    CHECK(write_bbt(&chip,buf,&primary,&mirror,-1)==-ENOSPC);
    /* Correctable BBT read rewrites both copies; EBADMSG chooses mirror. */
    for (int e=0;e<2;e++) {
        reset(false); CHECK(check_create(&chip,buf,&bad)==0);
        int p=primary.pages[0]; main_error[p]=e?-EBADMSG:-EUCLEAN;
        unsigned int before=writes;
        CHECK(check_create(&chip,buf,&bad)==0 && writes>before);
        CHECK(primary.pages[0]>=0 && primary.pages[0]!=mirror.pages[0]);
    }
    for (int perchip=0;perchip<2;perchip++) for (int absolute=0;absolute<2;absolute++) {
        reset(false);
        if (perchip) primary.options|=NAND_BBT_PERCHIP;
        mirror=primary; mirror.pattern=mirror_pattern;
        CHECK(check_create(&chip,buf,&bad)==0);
        if (absolute) primary.options|=NAND_BBT_ABSPAGE;
        primary.reserved_block_code=2;
        unsigned int before=writes;
        mark_bbt_region(&chip,&primary);
        CHECK(writes>before);
        for (int t=0;t<(perchip?TARGETS:1);t++) {
            int b=absolute?primary.pages[t]/PPB:(t+1)*(perchip?BLOCKS:BLOCKS*TARGETS)-4;
            for (int i=0;i<(absolute?1:4);i++) CHECK(bbt_get_entry(&chip,b+i)==BBT_BLOCK_RESERVED);
        }
        before=writes; mark_bbt_region(&chip,&primary); CHECK(writes==before);
    }
    static const unsigned versions[]={0,127,254,255};
    for (unsigned int i=0;i<ARRAY_SIZE(versions);i++) {
        reset(false); CHECK(check_create(&chip,buf,&bad)==0);
        primary.version[0]=mirror.version[0]=versions[i];
        CHECK(write_bbt(&chip,buf,&primary,&mirror,-1)==0);
        CHECK(write_bbt(&chip,buf,&mirror,&primary,-1)==0);
        bbt_mark_entry(&chip,7,BBT_BLOCK_WORN);
        primary.version[0]++;
        CHECK(write_bbt(&chip,buf,&primary,&mirror,-1)==0);
        /* Simulate loss of power after primary, before mirror, including wrap. */
        memset(ram_bbt,0,sizeof(ram_bbt)); search_read_bbts(&chip,buf,&primary,&mirror);
        CHECK(check_create(&chip,buf,&bad)==0 && bbt_get_entry(&chip,7)==BBT_BLOCK_WORN);
    }
    /* Real source silently drops create_bbt() transport failure. */
    for (int no_oob=0;no_oob<2;no_oob++) {
        reset(false);
        if (no_oob) { primary.options|=NAND_BBT_NO_OOB; primary.veroffs=4; primary.offs=0; }
        mirror=primary; mirror.pattern=mirror_pattern;
        page_error[PPB]=-EIO; spare[2*PPB][0]=0;
        CHECK(create_bbt(&chip,buf,&bad,-1)==-EIO);
        CHECK(check_create(&chip,buf,&bad)==0 && primary.pages[0]>=0);
        CHECK(bbt_get_entry(&chip,2)==BBT_BLOCK_GOOD);
    }
    puts("SOURCE WITNESS: check_create ignores scan EIO and writes an incomplete BBT");
    for (int no_oob=0;no_oob<2;no_oob++) {
        reset(false);
        if (no_oob) { primary.options|=NAND_BBT_NO_OOB; primary.veroffs=4; primary.offs=0; }
        mirror=primary; mirror.pattern=mirror_pattern;
        CHECK(check_create(&chip,buf,&bad)==0);
        bbt_mark_entry(&chip,7,BBT_BLOCK_WORN); CHECK(nand_update_bbt(&chip,0)==0);
        memset(ram_bbt,0,sizeof(ram_bbt)); main_error[primary.pages[0]]=-EIO;
        CHECK(check_create(&chip,buf,&bad)==0 && bbt_get_entry(&chip,7)==BBT_BLOCK_GOOD);
    }
    puts("SOURCE WITNESS: check_create accepts BBT read EIO without recovering the bad-block entries");
    puts("BBT: BBM page/width, PLACE_OOB, scan/create, mirrors, in-band/OOB, per-chip, update, relocation, reserved regions and version wrap checked");
}
static void compound_checks(void)
{
    u8 data[3*PAGE],oob[3*SPARE+16],buf[PPB*(PAGE+SPARE)];
    unsigned cases=0;
    for (int mode=0;mode<3;mode++) for (int with_oob=0;with_oob<2;with_oob++) {
        reset(true); retry_fail_page=0; page_error[1]=-EIO;
        memset(data,0xa5,sizeof(data)); memset(oob,0xa5,sizeof(oob));
        unsigned per=mode==MTD_OPS_AUTO_OOB?6:SPARE;
        struct mtd_oob_ops ops={.mode=mode,.len=3*PAGE,.datbuf=data,
            .oobbuf=with_oob?oob:NULL,.ooblen=with_oob?3*per:0};
        CHECK(nand_do_read_ops(&chip,0,&ops)==-EIO);
        CHECK(ops.retlen==PAGE && !memcmp(data,media[0],PAGE));
        CHECK(data[PAGE]==0xa5 && !chip.mtd.ecc_stats.failed && chip.selected==-1);
        CHECK(retry_mode==0 && retries==(mode==MTD_OPS_RAW?0:2));
        if (with_oob) {
            /* Source witness persists on error, not just successful reads. */
            CHECK(ops.oobretlen==(mode==MTD_OPS_RAW?per:2*per));
            CHECK(oob[3*per]==0xa5);
        }
        cases++;
    }
    for (int inband=0;inband<2;inband++) for (int exhausted=0;exhausted<2;exhausted++) {
        reset(false);
        if (inband) { primary.options|=NAND_BBT_NO_OOB; primary.veroffs=4; primary.offs=0; }
        mirror=primary; mirror.pattern=mirror_pattern;
        bbt_mark_entry(&chip,1,BBT_BLOCK_WORN);
        CHECK(write_bbt(&chip,buf,&primary,&mirror,-1)==0);
        CHECK(write_bbt(&chip,buf,&mirror,&primary,-1)==0);
        int p=primary.pages[0],m=mirror.pages[0];
        u8 saved_mirror[PAGE]; memcpy(saved_mirror,media[m],PAGE);
        bbt_mark_entry(&chip,7,BBT_BLOCK_WORN);
        compound_erase_fail[p/PPB]=true;
        /* Initial destination fails erase; its replacement fails program. */
        compound_program_fail[(BLOCKS*TARGETS-3)*PPB]=true;
        if (exhausted) compound_program_fail[(BLOCKS*TARGETS-4)*PPB]=true;
        int ret=nand_update_bbt(&chip,0);
        CHECK(ret==(exhausted?-ENOSPC:0));
        CHECK(primary.pages[0]!=mirror.pages[0]);
        if (exhausted) {
            CHECK(!memcmp(saved_mirror,media[m],PAGE));
            memset(ram_bbt,0,sizeof(ram_bbt));
            CHECK(read_abs_bbt(&chip,buf,&mirror,-1)==0);
            CHECK(bbt_get_entry(&chip,1)==BBT_BLOCK_WORN);
            /* Failed updates make no durability promise for the new mark. */
            CHECK(bbt_get_entry(&chip,7)==BBT_BLOCK_GOOD);
        } else {
            memset(ram_bbt,0,sizeof(ram_bbt)); search_read_bbts(&chip,buf,&primary,&mirror);
            CHECK(check_create(&chip,buf,&bad)==0);
            CHECK(bbt_get_entry(&chip,1)==BBT_BLOCK_WORN && bbt_get_entry(&chip,7)==BBT_BLOCK_WORN);
            CHECK(bbt_get_entry(&chip,p/PPB)!=BBT_BLOCK_GOOD);
            CHECK(bbt_get_entry(&chip,BLOCKS*TARGETS-3)!=BBT_BLOCK_GOOD);
        }
        cases++;
    }
    printf("compound failures: %u retry/transport and BBT erase/replacement/exhaustion/restart cases\n",cases);
}
int main(void)
{
    request_checks(); bbt_checks(); compound_checks();
    printf("ALL CORE CHECKS PASSED (%u assertions)\n",checks);
    return 0;
}
