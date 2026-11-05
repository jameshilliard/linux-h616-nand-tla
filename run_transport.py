#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Core requests -> page callbacks -> core command helpers -> parser -> MMIO.

Separate translation units preserve each existing harness's private structs;
the adapter passes values, not pointers to incompatible kernel structures.
"""
import argparse
from pathlib import Path
import re
from boundary_common import HERE, bodies, check
from driver_boundary import fixture_body
from run_composed import units as composed_units
from run_c import chunk_functions

CORE = ["nand_fill_column_cycles", "nand_sp_exec_read_page_op",
        "nand_lp_exec_read_page_op", "nand_read_page_op", "nand_read_oob_op",
        "nand_change_read_column_op", "nand_exec_prog_page_op",
        "nand_prog_page_begin_op", "nand_prog_page_end_op", "nand_change_write_column_op",
        "nand_status_op", "nand_set_features_op", "nand_set_features",
        "nand_read_data_op", "nand_write_data_op", "nand_read_page_raw", "nand_write_page_raw"]
LOW = ["sunxi_nfc_interrupt", "sunxi_nfc_wait_events", "sunxi_nfc_wait_cmd_fifo_empty",
       "sunxi_nfc_dma_op_prepare", "sunxi_nfc_dma_op_cleanup", "sunxi_nfc_dma_op_abort",
       "sunxi_nfc_rst", "sunxi_nfc_select_chip", "sunxi_nfc_read_buf", "sunxi_nfc_write_buf",
       "sunxi_nfc_exec_subop", "sunxi_nfc_soft_waitrdy", "sunxi_nfc_exec_op"]
FUNCTIONS = {"sunxi_nand.c": LOW, "nand_base.c": CORE,
             "internals.h": ["nand_has_exec_op", "nand_check_op", "nand_exec_op"],
             "nand_hynix.c": ["hynix_nand_cmd_op", "hynix_nand_reg_write_op", "hynix_nand_setup_read_retry"],
             "nand_micron.c": ["micron_nand_setup_read_retry"]}


def replace_once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new, 1)


def units(candidate_source=None):
    unit, hashes, src = composed_units(candidate_source)
    source = src["sunxi_nand.c"]
    defines = []
    for filename, pattern in (("sunxi_nand.c", "NFC_|NDFC_"),
                              ("rawnand.h", "NAND_CMD_|NAND_STATUS_|NAND_BUSWIDTH_16|NAND_ROW_ADDR_3|NAND_OP_|NAND_OPERATION|NAND_DESTRUCTIVE_OPERATION"),
                              ("onfi.h", "ONFI_SUBFEATURE_PARAM_LEN|ONFI_FEATURE_ADDR_READ_RETRY"),
                              ("nand_hynix.c", "NAND_HYNIX_CMD_")):
        for m in re.finditer(r"^#define (?:" + pattern + r")[^\n\\]*(?:\\\n[^\n\\]*)*", src[filename], re.M):
            # Parser pattern macros come from the separately pinned header.
            if "NAND_OP_PARSER" not in m[0]:
                defines.append(m[0])
    header = (HERE / "tests/lowlevel.h").read_text()
    header = replace_once(header, "struct sunxi_nfc {", "struct nand_chip; struct nand_operation;\n"
        "struct nand_controller_ops { int (*exec_op)(struct nand_chip *, const struct nand_operation *, bool); };\n"
        "struct sunxi_nfc {\n    const struct nand_controller_ops *ops;")
    header = replace_once(header, "unsigned writesize, erasesize, ecc_step_size;",
                          "unsigned writesize, erasesize, ecc_step_size, oobsize;")
    header = replace_once(header, "unsigned page_shift, options;", "unsigned page_shift, options, cur_cs;\n"
        "    struct { unsigned ntargets; } base;\n"
        "    void *manufacturer; u8 *oob_poi;\n"
        "    struct { void (*cmdfunc)(struct nand_chip *, unsigned, int, int);\n"
        "        void (*read_buf)(struct nand_chip *, void *, unsigned);\n"
        "        void (*write_buf)(struct nand_chip *, const void *, unsigned);\n"
        "        u8 (*read_byte)(struct nand_chip *);\n"
        "        void (*write_byte)(struct nand_chip *, u8);\n"
        "        int (*waitfunc)(struct nand_chip *);\n"
        "        int (*set_features)(struct nand_chip *, int, u8 *); } legacy;")
    header = replace_once(header, "unsigned type;", "unsigned type, delay_ns;")
    header = replace_once(header, "} buf; unsigned len;", "} buf; unsigned len; bool force_8bit;")
    header = replace_once(header, "static u8 registers[0x3000], media[2][20000];", "")
    header = replace_once(header, "/* @parser-header@ */", (HERE / "reference/nand_parser.h").read_text())
    header = replace_once(header, "{ hw.submit++; return hw.submit_fail ? -EIO : 1; }",
        "{ assert(d == &descriptor); hw.submit++; return hw.submit_fail ? -EIO : 1; }")
    # The real dispatch in internals.h is included, not a command-level mock.
    header += '\n' + (HERE / "tests/transport.h").read_text()
    functions = dict(FUNCTIONS, **{"sunxi_nand.c": chunk_functions(source) + LOW})
    code, proto, added_hashes = bodies(src, functions)
    hashes.update(added_hashes)
    start = source.index("static const struct nand_op_parser sunxi_nfc_op_parser")
    end = source.index("static int sunxi_nfc_exec_op", start)
    parser_def = source[start:end]
    parser_code = (HERE / "reference/nand_parser.c").read_text()
    unit["transport.c"] = ("\n".join(defines) + "\n" + header + "\n" + proto + "\n" +
        parser_code + "\n" + parser_def + "\n" + code +
        '\n#line 1 "transport.c"\n' + (HERE / "tests/transport.c").read_text())
    driver = unit["driver.c"]
    driver = '#include "transport_api.h"\n' + driver
    # Accommodate the driver's advertised 8 KiB aperture to observe baseline
    # over-reads; the manual does not establish an 8 KiB RAM0 bank.
    driver = replace_once(driver, "u8 regs[0x1000];", "u8 regs[0x3000];")
    replacements = {
        "sunxi_nfc_select_chip": "if (select_error) return select_error; return wire_select(cs);",
        "nand_read_page_op": "if ((int)page == fail_page) return -EIO; return wire_read(n->cur_cs, page, col, dst, len);",
        "nand_change_read_column_op": "return wire_read_column(n->cur_cs, col, dst, len);",
        "nand_prog_page_begin_op": "return wire_begin(n->cur_cs, page, col, data, len);",
        "nand_change_write_column_op": "return wire_write_column(n->cur_cs, col, data, len);",
        "nand_prog_page_end_op": "return wire_end(n->cur_cs);",
        "sunxi_nfc_write_buf": "return wire_write_buf(n->cur_cs, data, len);",
        "sunxi_nfc_read_buf": "plain_read_calls++; return wire_read_buf(n->cur_cs, dst, len);",
    }
    for name, body in replacements.items():
        old = fixture_body(driver, name)
        driver = driver.replace(old, old[:old.index("{")] + "{ " + body + " }", 1)
    driver = replace_once(driver, "if(off==NFC_REG_CMD) command(value);",
        "if (off == 0x04) { u32 v = wire_old_status & ~(value & 7); memcpy(ptr,&v,4); }\n"
        "    if(off==NFC_REG_CMD) { if (value >> 30) command(value); else wire_command(value); }")
    driver = replace_once(driver, "mmio_writes[off/4]++; memcpy(ptr,&value,4);",
        "u32 wire_old_status; memcpy(&wire_old_status, ptr, 4);\n"
        "    if (off == 0x20 && persistent_caps.reg_user_data_len) value &= 0x3ff;\n"
        "    mmio_writes[off/4]++; memcpy(ptr,&value,4);")
    driver = replace_once(driver, "static void writel(u32 value,void *ptr)\n{",
        "static void wire_command(u32 value);\nstatic void writel(u32 value,void *ptr)\n{")
    driver = replace_once(driver, "fail_page=ecc_error_page=-1; command_count=operation_count=0;",
        "fail_page=ecc_error_page=-1; command_count=operation_count=0;\n"
        "    wire_reset(controller.regs, variable);")
    raw_start = driver.index("        sunxi_nfc_select_chip(n,cs); row=page;")
    raw_end = driver.index("    } else {\n        n->ecc.read_page", raw_start)
    driver = driver[:raw_start] + (
        "        ret=(int)page==fail_page?-EIO:\n"
        "            wire_raw(write,cs,page,data,n->oob_poi,need_oob);\n") + driver[raw_end:]
    driver += '\n#line 1 "transport_media.c"\n' + (HERE / "tests/transport_media.c").read_text()
    unit["driver.c"] = driver
    unit["transport_api.h"] = (HERE / "tests/transport_api.h").read_text()
    unit["check.c"] = replace_once(unit["check.c"], "int main(void)", "void composed_main(void)")
    unit["check.c"] = "static unsigned transport_bounced, transport_direct;\n" + unit["check.c"]
    unit["check.c"] = replace_once(unit["check.c"], "    callbacks++; return ret;",
        "    if(b) { if(b==c->data_buf) transport_bounced++; else transport_direct++; }\n"
        "    callbacks++; return ret;")
    unit["check.c"] += ('\n#include "transport_api.h"\n' +
        (HERE / "tests/transport_core.c").read_text() +
        '\nint main(void) { composed_main(); core_retry_tests(); core_copy_tests(); wire_tests(); return 0; }\n')
    return unit, hashes, src


def count_boundary_units(unit):
    """Admitted synthetic large-OOB geometry; not a claimed physical NAND part."""
    unit["driver.c"] = replace_once(unit["driver.c"], "B_SPARE=256", "B_SPARE=2048")
    unit["transport.c"] = replace_once(unit["transport.c"], ".oobsize=256", ".oobsize=2048")
    unit["driver.c"] += '\n#line 1 "count_boundary.c"\n' + (
        HERE / "tests/count_boundary.c").read_text()
    # Keep the normal 2 KiB+256 request/retry matrices separate. The new matrix
    # reaches actual attach, PIO page/OOB, command, parser and IRQ bodies.
    unit["check.c"] = replace_once(unit["check.c"],
        "int main(void) { composed_main(); core_retry_tests(); core_copy_tests(); wire_tests(); return 0; }",
        "extern void count_boundary_tests(void);\n"
        "int main(void) { count_boundary_tests(); return 0; }")
    return unit


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source", type=Path)
    parser.add_argument("--count-boundary", action="store_true",
                        help="exercise admitted large-OOB geometry against NDFC_CNT[9:0]")
    parser.add_argument("--expect-count-fixed", action="store_true",
                        help="require complete large-OOB transfers rather than baseline counterexamples")
    args = parser.parse_args()
    if args.expect_count_fixed and not (args.candidate_source and args.count_boundary):
        parser.error("--expect-count-fixed requires --candidate-source and --count-boundary")
    unit, hashes, src = units(args.candidate_source)
    if args.count_boundary:
        unit = count_boundary_units(unit)
        unit["driver.c"] = f"#define FIXED_COUNT {int(args.expect_count_fixed)}\n" + unit["driver.c"]
    names = {}
    for key in hashes:
        filename, name = key.split(":", 1)
        names.setdefault(filename, []).append(name)
    check("transport-count" if args.count_boundary else "transport", unit, hashes,
          coverage=(src, names), require_all_called=False,
          limits=["Value adapters, not kernel structure ABI/locking execution",
                  "Actual NAND command helpers, dispatch, parser and sunxi exec_op/IRQ bodies",
                  "Persistent emulated media; opaque ECC parity and DMA transfers remain fixtures",
                  "SDR exec_op only; legacy callbacks, cache reads and physical timing excluded",
                  "Manufacturer parameter commit/retention are explicit emulator contracts",
                  "Large-OOB count witness uses an admitted synthetic geometry, not a physical NAND part"])
