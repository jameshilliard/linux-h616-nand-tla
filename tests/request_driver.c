/* SPDX-License-Identifier: GPL-2.0-or-later */
static FILE *request_output;
static bool request_enabled, request_first;
static bool request_fail_raw;
static unsigned request_raw_errors;

void request_read_fault(unsigned mode)
{
    assert(mode<=2);
    ecc_error_page=mode ? 17 : -1;
    request_fail_raw=mode==2;
    request_raw_errors=0;
    media_wire_fault(0,0);
}

unsigned request_wire_errors(void) { return request_raw_errors; }
void request_wire_error(void)
{
    request_raw_errors++;
    request_event("wire-error",-ETIMEDOUT,0);
}

void request_after_ecc(void)
{
    /* Fail the next normal command after each ECC batch, including a PIO
     * retry after DMA. No source-level return value is substituted. */
    if(request_fail_raw) media_wire_fault(1,0);
}

void request_event(const char *kind, int arg, int extra)
{
    if (!request_enabled) return;
    unsigned mapped,active,unsafe,mask;
    request_snapshot(&mapped,&active,&unsafe,&mask);
    fprintf(request_output,"%s[\"%s\",%d,%d,%u,%u,%u,%u,%u]",
            request_first ? "" : ",",kind,arg,extra,mapped,active,unsafe,mask,
            readl(controller.regs+NFC_REG_ECC_CTL)&(NFC_ECC_EN|NFC_RANDOM_EN(&controller)));
    request_first=false;
}

void request_driver_begin(unsigned id, unsigned fmt, unsigned backend, unsigned fault,
                          unsigned length, unsigned ooblen)
{
    if (!request_output) { request_output=fopen("request-traces.jsonl","w"); assert(request_output); }
    fprintf(request_output,"{\"id\":%u,\"format\":%u,\"backend\":%u,\"fault\":%u,\"len\":%u,\"ooblen\":%u,\"events\":[",
            id,fmt,backend,fault,length,ooblen);
    request_enabled=request_first=true;
    request_event("begin",0,0);
}

void request_driver_end(int ret, unsigned length, unsigned ooblen)
{
    request_event("result",ret,length);
    fprintf(request_output,"],\"ret\":%d,\"retlen\":%u,\"oobretlen\":%u}\n",ret,length,ooblen);
    fflush(request_output); request_enabled=false;
}

void request_seed_media(unsigned page, const unsigned char *raw)
{
    assert(page<B_PAGES);
    for(unsigned cs=0;cs<B_TARGETS;cs++) memcpy(flash[cs][page],raw,B_PAGE+B_SPARE);
}

void request_dump_media(unsigned page)
{
    FILE *f=fopen("request-media.bin","ab"); assert(f);
    assert(fwrite(flash[0][page],B_PAGE+B_SPARE,1,f)==1); assert(!fclose(f));
}

int request_subpage(bool write, bool dma, unsigned cs, unsigned char *buf,
                    unsigned off, unsigned length)
{
    struct nand_chip *n=&persistent_chip;
    n->cur_cs=cs;
    request_event("page-enter",write,17);
    int ret=write ? sunxi_nfc_hw_ecc_write_subpage(n,off,length,buf,0,17) :
        dma ? sunxi_nfc_hw_ecc_read_subpage_dma(n,off,length,buf,17) :
              sunxi_nfc_hw_ecc_read_subpage(n,off,length,buf,17);
    request_event("page-exit",ret,0);
    return ret;
}

void request_finish(void)
{
    free(persistent_chip.user_data_bytes); persistent_chip.user_data_bytes=NULL;
    assert(request_output && !fclose(request_output)); request_output=NULL;
}
