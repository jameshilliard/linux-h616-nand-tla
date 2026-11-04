/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
/* Value-only boundary between independently extracted kernel translation units. */
void request_event(const char *, int, int);
void request_snapshot(unsigned *, unsigned *, unsigned *, unsigned *);
void request_config(unsigned, unsigned, unsigned);
int request_prepare(const void *, unsigned, unsigned, int);
int request_wait(unsigned, bool, unsigned);
int request_fifo(void);
void request_abort(void);
void request_cleanup(int);
void request_launch(bool);
void request_reuse(unsigned);
void request_read_fault(unsigned);
void request_after_ecc(void);
unsigned request_wire_errors(void);
void request_wire_error(void);
void request_driver_begin(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
void request_driver_end(int, unsigned, unsigned);
void request_seed_media(unsigned, const unsigned char *);
void request_dump_media(unsigned);
int request_subpage(bool, bool, unsigned, unsigned char *, unsigned, unsigned);
void request_finish(void);
