/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "request_api.h"
/* The real NAND core decides callback, column, retry/copy and return lengths.
 * All raw seed images are independently produced by Python, never by a
 * write/read round trip through the implementation under test. */
int main(void)
{
    unsigned id=0;
    u8 raw[PAGE+SPARE], data[PAGE], output[PAGE+2], metadata[SPARE];
    FILE *input=fopen("request-vectors.bin","rb"); assert(input);
    FILE *media_out=fopen("request-media.bin","wb"); assert(media_out && !fclose(media_out));
    for(unsigned fmt=0;fmt<3;fmt++)
    for(unsigned backend=0;backend<3;backend++) {
        assert(fread(raw,sizeof(raw),1,input)==1);
        for(unsigned i=0;i<PAGE;i++) data[i]=(i*19+(i>>8)*7+fmt)&255;
        for(unsigned op=0;op<6;op++)
        for(unsigned fault=0;fault<5;fault++) {
            if(fault==3) continue; /* Failed stop is a separate conditional model. */
            setup(fmt,backend==2); use_dma=op%2;
            request_config(backend,fault,1);
            request_seed_media(17,raw);
            memset(output,0xa5,sizeof(output)); memset(metadata,0xff,sizeof(metadata));
            struct mtd_oob_ops request={.mode=MTD_OPS_AUTO_OOB,
                .len=op>=4 ? 0 : PAGE,.datbuf=op>=4 ? NULL : (op>=2 ? data : output+1),
                .ooblen=chip.mtd.oobavail,.oobbuf=metadata};
            request_driver_begin(id++,fmt,backend,fault,request.len,request.ooblen);
            int ret=op<2 ? nand_do_read_ops(&chip,17*PAGE,&request) :
                    op<4 ? nand_do_write_ops(&chip,17*PAGE,&request) :
                    nand_do_read_oob(&chip,17*PAGE,&request);
            if(!ret && op<2) assert(!memcmp(output+1,data,PAGE));
            assert(output[0]==0xa5 && output[PAGE+1]==0xa5);
            assert(request.retlen<=request.len && request.oobretlen<=request.ooblen);
            request_driver_end(ret,request.retlen,request.oobretlen);
            if(op>=2 && op<4 && !fault) request_dump_media(17);

            /* Recover twice without clearing controller/media state. */
            for(unsigned retry=0;retry<2;retry++) {
                request_config(backend,0,0); request_reuse(retry);
                driver_faults(-1,-1); chip.pagecache.page=-1;
                request=(struct mtd_oob_ops){.mode=MTD_OPS_AUTO_OOB,.len=PAGE,.datbuf=output+1};
                request_driver_begin(id++,fmt,backend,0,request.len,request.ooblen);
                ret=nand_do_read_ops(&chip,(loff_t)(17+retry*BLOCKS*PPB)*PAGE,&request);
                assert(!ret && request.retlen==PAGE);
                request_driver_end(ret,request.retlen,request.oobretlen);
            }
        }
        /* Invoke actual subpage entrypoints, not a full-page stand-in. */
        for(unsigned dma=0;dma<2;dma++) {
            setup(fmt,backend==2); request_config(backend,0,0);
            request_seed_media(17,raw);
            request_driver_begin(id++,fmt,backend,0,511,0);
            int ret=request_subpage(false,dma,0,output+1,1025,511);
            assert(!ret && !memcmp(output+1+1024,data+1024,1024));
            request_driver_end(ret,511,0);
        }
        /* An uncorrectable step must reach the core as -EBADMSG. If its
         * raw erased-page reread instead times out, that transport error
         * must survive the DMA-to-PIO retry without invented ECC failures. */
        for(unsigned dma=0;dma<2;dma++)
        for(unsigned oob=0;oob<2;oob++)
        for(unsigned failure=1;failure<=2;failure++) {
            setup(fmt,backend==2); use_dma=dma;
            request_config(backend,0,0); request_seed_media(17,raw);
            request_read_fault(failure);
            struct mtd_oob_ops request={.mode=MTD_OPS_AUTO_OOB,
                .len=PAGE,.datbuf=output+1,.ooblen=oob ? chip.mtd.oobavail : 0,
                .oobbuf=oob ? metadata : NULL};
            request_driver_begin(id++,fmt,backend,4+failure,PAGE,request.ooblen);
            int ret=nand_do_read_ops(&chip,17*PAGE,&request);
            /* Vendor DMA classifies a data-only failure from the saved
             * batch result; no raw normal command is needed in that path. */
            bool normal_error=failure==2 && !(fmt==2 && dma && !oob);
            if(!normal_error) {
                assert(ret==-EBADMSG && request.retlen==PAGE);
                assert(chip.mtd.ecc_stats.failed && !chip.mtd.ecc_stats.corrected);
                assert(!request_wire_errors());
            } else {
                assert(ret==-ETIMEDOUT && !request.retlen);
                assert(!chip.mtd.ecc_stats.failed && !chip.mtd.ecc_stats.corrected);
                assert(request_wire_errors());
            }
            request_driver_end(ret,request.retlen,request.oobretlen);
            request_read_fault(0); chip.pagecache.page=-1;
            request_config(backend,0,0);
            request_driver_begin(id++,fmt,backend,0,PAGE,request.ooblen);
            ret=nand_do_read_ops(&chip,17*PAGE,&request);
            assert(!ret && request.retlen==PAGE && !memcmp(output+1,data,PAGE));
            request_driver_end(ret,request.retlen,request.oobretlen);
        }
    }
    assert(fgetc(input)==EOF); assert(!fclose(input));
    /* Release the last attach allocation without requiring a particular
     * operation count in a fixture shared with the original composed tests. */
    request_finish();
    printf("Whole request source traces: %u actual page/OOB/subpage and reuse executions\n",id);
    puts("ALL CHECKS PASSED");
    return 0;
}
