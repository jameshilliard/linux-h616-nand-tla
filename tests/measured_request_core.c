/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "request_api.h"
extern void measured_options(bool,unsigned),measured_layout(unsigned *,unsigned *,unsigned *);
extern void measured_store(FILE *);
extern bool measured_erased(void);
extern unsigned measured_confirm_count(void);

int main(void)
{
    unsigned id=0;
    u8 data[PAGE],out[PAGE+2],metadata[SPARE],got[SPARE+2];
    FILE *binary=fopen("measured-pages.bin","wb"); assert(binary);
    FILE *report=fopen("measured-pages.jsonl","w"); assert(report);
    for (unsigned fmt=0;fmt<3;fmt++)
    for (unsigned writer=0;writer<2;writer++)
    for (unsigned reader=0;reader<2;reader++) {
        setup(fmt,true); measured_options(false,0); request_config(2,0,0);
        unsigned first,second,tail;
        measured_layout(&first,&second,&tail);
        for (unsigned i=0;i<PAGE;i++) data[i]=(i*19+(i>>8)*7+fmt)&255;
        memset(metadata,255,sizeof(metadata)); use_dma=writer;
        struct mtd_oob_ops op={.mode=MTD_OPS_AUTO_OOB,.len=PAGE,.datbuf=data,
            .ooblen=chip.mtd.oobavail,.oobbuf=metadata};
        request_driver_begin(id++,fmt,2,0,PAGE,op.ooblen);
        int ret=nand_do_write_ops(&chip,17*PAGE,&op);
        request_driver_end(ret,op.retlen,op.oobretlen);
        assert(!ret && op.retlen==PAGE && op.oobretlen==chip.mtd.oobavail);
        assert(measured_confirm_count()==1);
        measured_store(binary);

        memset(out,0xa5,sizeof(out)); memset(got,0xa5,sizeof(got));
        use_dma=reader; request_config(2,0,0); chip.pagecache.page=-1;
        op=(struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB,.len=PAGE,.datbuf=out+1,
            .ooblen=chip.mtd.oobavail,.oobbuf=got+1};
        request_driver_begin(id++,fmt,2,0,PAGE,op.ooblen);
        ret=nand_do_read_ops(&chip,17*PAGE,&op);
        request_driver_end(ret,op.retlen,op.oobretlen);
        assert(!ret && op.retlen==PAGE && op.oobretlen==chip.mtd.oobavail);
        assert(!memcmp(out+1,data,PAGE));
        assert(out[0]==0xa5 && out[PAGE+1]==0xa5 && got[0]==0xa5 && got[op.oobretlen+1]==0xa5);
        assert(fwrite(out+1,PAGE,1,binary)==1);
        assert(fwrite(got+1,op.oobretlen,1,binary)==1);
        /* AUTO_OOB exposes only protected free bytes, not the extra tail.
         * PLACE_OOB is required to observe that tail through the core. */
        chip.pagecache.page=-1;
        op=(struct mtd_oob_ops){.mode=MTD_OPS_PLACE_OOB,.len=PAGE,.datbuf=out+1,
            .ooblen=SPARE,.oobbuf=got+1};
        request_driver_begin(id++,fmt,2,0,PAGE,SPARE);
        ret=nand_do_read_ops(&chip,17*PAGE,&op);
        request_driver_end(ret,op.retlen,op.oobretlen);
        assert(!ret && op.retlen==PAGE && op.oobretlen==SPARE);
        assert(!memcmp(out+1,data,PAGE));
        assert(out[0]==0xa5 && out[PAGE+1]==0xa5 && got[0]==0xa5 && got[SPARE+1]==0xa5);
        assert(fwrite(got+1+tail,SPARE-tail,1,binary)==1 || SPARE==tail);
        fprintf(report,"{\"kind\":\"roundtrip\",\"format\":%u,\"writer\":%u,\"reader\":%u,\"spare\":%u,\"user\":[%u,%u],\"tail\":%u,\"free\":%u}\n",
                fmt,writer,reader,SPARE,first,second,tail,chip.mtd.oobavail);
    }

    /* Keep actual MMIO reset + whole-page DMA-to-PIO retry. Only the hardware
     * data-path contract changes; successful status is not replaced in C. */
    for (unsigned dirty=0;dirty<2;dirty++)
    for (unsigned failure=2;failure<=4;failure+=2) {
        setup(0,true); measured_options(false,0); request_config(2,0,0); use_dma=true;
        for (unsigned i=0;i<PAGE;i++) data[i]=(i*19+(i>>8)*7)&255;
        struct mtd_oob_ops op={.mode=MTD_OPS_AUTO_OOB,.len=PAGE,.datbuf=data};
        request_driver_begin(id++,0,2,0,PAGE,0);
        int ret=nand_do_write_ops(&chip,17*PAGE,&op);
        request_driver_end(ret,op.retlen,op.oobretlen); assert(!ret);
        measured_options(dirty,0); request_config(2,failure,1);
        for (unsigned attempt=0;attempt<3;attempt++) {
            if (attempt) request_config(2,0,0);
            memset(out,0xa5,sizeof(out)); chip.pagecache.page=-1;
            op=(struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB,.len=PAGE,.datbuf=out+1};
            request_driver_begin(id++,0,2,attempt?0:failure,PAGE,0);
            ret=nand_do_read_ops(&chip,17*PAGE,&op);
            request_driver_end(ret,op.retlen,op.oobretlen);
            if (!attempt && failure==4) assert(ret==-ETIMEDOUT && !op.retlen);
            else {
                assert(!ret && op.retlen==PAGE);
                assert((memcmp(out+1,data,PAGE)!=0)==(dirty && !attempt));
                assert(!memcmp(out+1+1024,data+1024,1024));
            }
            assert(out[0]==0xa5 && out[PAGE+1]==0xa5);
            fprintf(report,"{\"kind\":\"retry\",\"dirty\":%u,\"failure\":%u,\"attempt\":%u,\"ret\":%d,\"length\":%zu,\"mismatch\":%u}\n",
                    dirty,failure,attempt,ret,op.retlen,(unsigned)(memcmp(out+1,data,PAGE)!=0));
        }
    }

    /* A rejected or partial tail must never be confirmed. A confirm accepted
     * before its timeout may have changed media despite the request error. */
    for (unsigned writer=0;writer<2;writer++)
    for (unsigned fault=1;fault<=3;fault++) {
        setup(2,true); measured_options(false,fault); request_config(2,0,0); use_dma=writer;
        memset(metadata,255,sizeof(metadata));
        struct mtd_oob_ops op={.mode=MTD_OPS_AUTO_OOB,.len=PAGE,.datbuf=data,
            .ooblen=chip.mtd.oobavail,.oobbuf=metadata};
        request_driver_begin(id++,2,2,fault,PAGE,op.ooblen);
        int ret=nand_do_write_ops(&chip,17*PAGE,&op);
        request_driver_end(ret,op.retlen,op.oobretlen);
        assert(ret==-ETIMEDOUT && !op.retlen);
        assert(measured_confirm_count()==(fault==3));
        assert(measured_erased()==(fault!=3));
        fprintf(report,"{\"kind\":\"write-failure\",\"writer\":%u,\"fault\":%u,\"confirmed\":%u,\"erased\":%u}\n",
                writer,fault,measured_confirm_count(),measured_erased());
        /* New PROGRAM BEGIN discards the old buffer. It does not roll back
         * an accepted confirm; exercise reuse on a different erased page. */
        measured_options(false,0); request_config(2,0,0);
        op.retlen=op.oobretlen=0;
        request_driver_begin(id++,2,2,0,PAGE,op.ooblen);
        ret=nand_do_write_ops(&chip,18*PAGE,&op);
        request_driver_end(ret,op.retlen,op.oobretlen);
        assert(!ret && op.retlen==PAGE && measured_confirm_count()==1);
    }
    assert(!fclose(binary) && !fclose(report)); request_finish();
    printf("Measured request composition: %u actual core calls, spare=%u\n",id,SPARE);
    puts("ALL CHECKS PASSED"); return 0;
}
