#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Public MTD wrappers -> NAND core -> sunxi callbacks -> command transport."""
import argparse
import json
from pathlib import Path

from boundary_common import HERE, bodies, check
from driver_boundary import fixture_body
from run_transport import replace_once, units as transport_units

FUNCTIONS = {
    "mtd.h": ["mtd_get_master_ofs", "mtd_div_by_ws"],
    "mtdcore.c": ["mtd_update_ecc_stats", "mtd_read", "mtd_write",
                  "mtd_check_oob_ops", "mtd_read_oob_std", "mtd_write_oob_std",
                  "mtd_read_oob", "mtd_write_oob"],
    "nand_base.c": ["nand_read_oob", "nand_write_oob"],
    "mtdchar.c": ["mtdchar_readoob", "mtdchar_writeoob"],
}


def units(candidate_source=None, candidate_mtd_core=None, expect_eof_rejection=False,
          expect_data_bounds=False, expect_partial_page_rejection=False):
    unit, hashes, src = transport_units(candidate_source)
    if candidate_mtd_core:
        src["mtdcore.c"] = candidate_mtd_core.read_text()
    core = unit["check.c"]
    core = replace_once(core, "#define WARN_ON_ONCE(x) (!!(x))",
                        "static unsigned boundary_warnings;\n"
                        "static bool boundary_warn(bool value) { boundary_warnings += value; return value; }\n"
                        "#define WARN_ON_ONCE(x) boundary_warn(!!(x))")
    # Extend only fixture structs. Extracted kernel function bodies are intact.
    stats = src["mtd.h"]
    start = stats.index("struct mtd_req_stats {")
    stats = stats[start:stats.index("\n};", start) + 3]
    core = replace_once(core, "struct mtd_oob_ops {", stats + "\nstruct mtd_oob_ops {")
    core = replace_once(core, "    u8 *datbuf, *oobbuf;",
                        "    u8 *datbuf, *oobbuf;\n    struct mtd_req_stats *stats;")
    core = replace_once(core, "struct mtd_info {", "struct mtd_info {\n" +
                        (HERE / "tests/mtd_fields.h").read_text())
    for name in ("mtd_read", "mtd_read_oob", "mtd_write_oob"):
        core = core.replace(fixture_body(core, name), "", 1)
        core = replace_once(core, "static int " + name + "(", "int " + name + "(")
    code, proto, added = bodies(src, FUNCTIONS)
    hashes.update(added)
    core = replace_once(core, '#line 1 "composed_core.c"',
                        (HERE / "tests/mtd_shims.h").read_text() + "\n" +
                        proto + "\n" + code + '\n#line 1 "composed_core.c"')
    core = replace_once(core, ".size=(loff_t)PAGE*PAGES,.ooblayout=&layout}",
                        ".size=(loff_t)PAGE*PAGES,.ooblayout=&layout,\n"
                        "        .flags=MTD_WRITEABLE,.writesize_shift=11,\n"
                        "        .ecc_strength=32,.bitflip_threshold=1,\n"
                        "        ._read_oob=nand_read_oob,._write_oob=nand_write_oob}")
    core = replace_once(core,
        "int main(void) { composed_main(); core_retry_tests(); core_copy_tests(); wire_tests(); return 0; }",
        '#line 1 "mtd_boundary.c"\n' + (HERE / "tests/mtd_boundary.c").read_text())
    unit["check.c"] = (f"#define EXPECT_EOF_REJECTION {int(expect_eof_rejection)}\n"
                       f"#define EXPECT_PARTIAL_PAGE_REJECTION {int(expect_partial_page_rejection)}\n"
                       f"#define EXPECT_DATA_BOUNDS {int(expect_data_bounds)}\n" + core)
    # Inject a simulated hardware ECC count at the existing MMIO boundary.
    # No kernel ECC or accounting implementation is replaced.
    driver = "static unsigned boundary_bitflips;\n" + unit["driver.c"]
    driver = replace_once(driver,
        "memset(controller.regs+persistent_caps.reg_ecc_err_cnt,0,8);",
        "memset(controller.regs+persistent_caps.reg_ecc_err_cnt,0,8);\n"
        "    memcpy(controller.regs+persistent_caps.reg_ecc_err_cnt,&boundary_bitflips,4);")
    driver += "\nvoid driver_bitflips(unsigned n) { assert(n <= 32); boundary_bitflips=n; }\n"
    # Shared transport fixtures retain optional trace hooks. They remain NULL
    # for this runner, so the ordinary MTD boundary behavior is unchanged.
    unit["driver.c"] = driver
    return unit, hashes, src


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source", type=Path)
    parser.add_argument("--candidate-mtd-core", type=Path,
                        help="extract the public MTD wrappers from this mtdcore.c")
    parser.add_argument("--expect-eof-rejection", action="store_true",
                        help="require rejection of nonempty OOB at EOF, independently of source")
    parser.add_argument("--expect-data-bounds", action="store_true",
                        help="require overflow-safe data bounds, independently of source")
    parser.add_argument("--expect-partial-page-rejection", action="store_true",
                        help="require rejection of OOB in a trailing partial page")
    args = parser.parse_args()
    unit, hashes, src = units(args.candidate_source, args.candidate_mtd_core,
                             args.expect_eof_rejection, args.expect_data_bounds,
                             args.expect_partial_page_rejection)
    names = {}
    for key in hashes:
        filename, name = key.split(":", 1)
        names.setdefault(filename, []).append(name)
    out = check("mtd-boundary", unit, hashes, coverage=(src, names), require_all_called=False,
          expected="ALL MTD BOUNDARY CHECKS PASSED", limits=[
              "Actual pinned OOB ioctl helpers, public MTD, partition translation and NAND entries",
              "Actual sunxi page/ECC/OOB/randomizer and SDR command/parser/IRQ bodies",
              "EOF OOB expectation: " + ("reject" if args.expect_eof_rejection else "baseline witness"),
              "Trailing partial-page OOB expectation: " + (
                  "reject" if args.expect_partial_page_rejection else "baseline witness"),
              "Data bounds expectation: " + ("overflow-safe" if args.expect_data_bounds
                                             else "baseline addition-overflow witness"),
              "Host fixture structures/value adapters, not kernel ABI or concurrent locking",
              "Page-aligned partition starts, including read-only trailing partial pages",
              "SLC-on-MLC dispatch is an assertion, not modeled",
              "ECC status and DMA/memory transport are emulated, not hardware verification",
              "Huge-length requests stop at the admission check and never access host buffers",
              "User-copy and allocation services are successful fixtures, not an actual ioctl syscall"])
    # Imported transport helpers need not all run here; every newly connected
    # public-interface helper must run in the separate source-coverage build.
    report = json.loads((out / "summary.json").read_text())
    required = [f"{filename}:{name}" for filename, functions in FUNCTIONS.items()
                for name in functions]
    missing = [name for name in required
               if not report["coverage"]["functions"][name]["calls"]]
    same_output = (out / "coverage-check.log").read_text() == report["output"]
    report["required_helpers"] = required
    report["uncalled_required_helpers"] = missing
    report["sanitizer_gcov_output_equal"] = same_output
    report["passed"] &= not missing and same_output
    (out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    if not report["passed"]:
        raise RuntimeError(f"MTD helper coverage/output mismatch: {missing}")


if __name__ == "__main__":
    main()
