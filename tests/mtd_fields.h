/* SPDX-License-Identifier: GPL-2.0-or-later */
    unsigned flags, ecc_strength, bitflip_threshold, writesize_shift;
    struct { u64 offset; } part;
    int (*_read_oob)(struct mtd_info *, loff_t, struct mtd_oob_ops *);
    int (*_write_oob)(struct mtd_info *, loff_t, struct mtd_oob_ops *);
    int (*_read)(struct mtd_info *, loff_t, size_t, size_t *, u8 *);
    int (*_write)(struct mtd_info *, loff_t, size_t, size_t *, const u8 *);
