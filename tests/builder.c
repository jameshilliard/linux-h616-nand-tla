/* SPDX-License-Identifier: GPL-2.0-or-later */
#define main image_builder_cli_main
#include "nand-image-builder.c"
#undef main
int main(int argc,char **argv)
{
    if (argc!=5) return 2;
    struct image_info info={.ecc_strength=atoi(argv[1]),.ecc_step_size=atoi(argv[2]),
        .page_size=2048,.usable_page_size=2048,.oob_size=512,.eraseblock_size=2048*64,
        .scramble=atoi(argv[3])};
    struct bch_control *b=init_bch(14,info.ecc_strength,BCH_PRIMITIVE_POLY);
    unsigned char payload[2048], buffer[2560];
    if (!b || fread(payload,1,sizeof(payload),stdin)!=sizeof(payload)) return 3;
    FILE *src=tmpfile(), *rnd=tmpfile(), *dst=tmpfile();
    if (!src || !rnd || !dst) return 4;
    if (fwrite(payload,1,sizeof(payload),src)!=sizeof(payload)) return 5;
    rewind(src);
    int ret=write_page(&info,buffer,src,rnd,dst,b,atoi(argv[4]));
    if (ret) return 6;
    rewind(dst);
    if (fread(buffer,1,sizeof(buffer),dst)!=sizeof(buffer)) return 7;
    if (fwrite(buffer,1,sizeof(buffer),stdout)!=sizeof(buffer)) return 8;
    fclose(src); fclose(rnd); fclose(dst); free_bch(b);
    return 0;
}
