/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fixture-only definitions. The SLC path must never silently become a stub. */
#define MTD_WRITEABLE 0x400
#define MTD_SLC_ON_MLC_EMULATION 0x4000
#define do_div(n, base) ({ unsigned rem = (n) % (base); (n) /= (base); rem; })
static void ledtrig_mtd_activity(void) {}
static int mtd_io_emulated_slc(struct mtd_info *m, loff_t o, bool r, struct mtd_oob_ops *p)
{ assert(!"SLC-on-MLC is outside this harness"); return -EINVAL; }
#define __user
#define MTD_FILE_MODE_RAW 3 /* enum mtd_file_modes in include/uapi/mtd/mtd-abi.h */
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define put_user(value, p) (*(p) = (value), 0)
struct mtd_file_info { struct mtd_info *mtd; int mode; };
struct file { void *private_data; };
static int copy_to_user(void *to, const void *from, size_t len)
{ memcpy(to, from, len); return 0; }
static void *memdup_user(const void *from, size_t len)
{
    void *data = malloc(len);
    assert(data);
    memcpy(data, from, len);
    return data;
}
