/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bch.c"
/* Binary command interface. All correction/encoding calls use the unmodified
 * reference library. Only the allocation/endian/kernel-log shims are hosted. */
int main(int argc, char **argv)
{
    if (argc!=4) return 2;
    int t=atoi(argv[2]), len=atoi(argv[3]);
    if (len<1 || len>2048) return 2;
    struct bch_control *b=bch_init(14,t,0x5803,true);
    if (!b) return 3;
    unsigned char data[2048], parity[160]={0};
    unsigned locations[80];
    if (fread(data,1,len,stdin)!=(size_t)len) return 4;
    if (!strcmp(argv[1],"encode")) {
        bch_encode(b,data,len,parity);
        if (fwrite(parity,1,b->ecc_bytes,stdout)!=b->ecc_bytes) return 5;
    } else {
        if (fread(parity,1,b->ecc_bytes,stdin)!=b->ecc_bytes) return 4;
        int ret=bch_decode(b,data,len,parity,NULL,NULL,locations);
        printf("%d\n",ret);
        if (ret>=0) {
            for (int i=0;i<ret;i++) if (locations[i]<(unsigned)len*8)
                data[locations[i]/8]^=1U<<(locations[i]%8);
            if (fwrite(data,1,len,stdout)!=(size_t)len) return 5;
        }
    }
    bch_free(b);
    return 0;
}
