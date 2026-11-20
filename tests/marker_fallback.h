/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Interface between the source-extracted controller and NAND core fixtures. */
struct hamming_geometry {
    unsigned step, bytes, steps;
    int controller_layout, core_marker_in_parity;
};
struct hamming_geometry hamming_geometry(int on_host, int controller_layout);
