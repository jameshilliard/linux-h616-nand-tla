/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
void wire_reset(void *, bool);
int wire_select(unsigned);
int wire_read(unsigned, unsigned, unsigned, void *, unsigned);
int wire_read_column(unsigned, unsigned, void *, unsigned);
int wire_begin(unsigned, unsigned, unsigned, const void *, unsigned);
int wire_write_column(unsigned, unsigned, const void *, unsigned);
int wire_write_buf(unsigned, const void *, unsigned);
int wire_read_buf(unsigned, void *, unsigned);
int wire_end(unsigned);
int wire_raw(bool, unsigned, unsigned, void *, void *, bool);
void wire_tests(void);
void wire_retry_start(bool, int, unsigned, unsigned);
int wire_retry_setup(unsigned);
uint32_t media_read_reg(unsigned);
void media_write_reg(unsigned, uint32_t);
void media_wire_reset(void);
void media_wire_fault(unsigned, unsigned);
unsigned media_wire_count(void);
void media_retry_snapshot(uint8_t *, uint8_t *, unsigned *, unsigned *);
