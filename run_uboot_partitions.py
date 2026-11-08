#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run U-Boot's partition fixups with libfdt and sanitizers; no hardware I/O."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="U-Boot tree (default: pinned extracted functions)")
    parser.add_argument("--save-extracted", type=Path, help="save the functions tested")
    parser.add_argument("--revision")
    parser.add_argument("--expect-failure", action="store_true")
    parser.add_argument("--input-dtb", type=Path,
                        help="also check the real H616 board handoff DT")
    parser.add_argument("--output-dtb", type=Path,
                        help="save the alias-free board DT (requires --input-dtb)")
    args = parser.parse_args()
    if args.output_dtb and not args.input_dtb:
        parser.error("--output-dtb requires --input-dtb")

    def read(path):
        if args.revision:
            return subprocess.check_output(
                ["git", "show", args.revision + ":" + path],
                cwd=args.source, text=True)
        return (args.source / path).read_text()

    if args.source:
        support = read("boot/fdt_support.c")
        board = read("board/sunxi/board.c")
        fdtdec = read("lib/fdtdec.c")
        extracted = function(fdtdec, "int fdtdec_get_is_enabled(")
        extracted += function(fdtdec, "u64 fdtdec_get_number(")
        extracted += function(support, "u32 fdt_getprop_u32_default_node(")
        extracted += function(support, "int fdt_increase_size(")
        start = support.index("static int fdt_del_subnodes(")
        end = support.index("\n#endif", start)
        extracted += support[start:end] + "\n"
        start = board.index("static int sunxi_fdt_prepare_fixed_partitions(")
        end = board.index("\nvoid i2c_init_board(", start)
        extracted += board[start:end]
    else:
        if args.revision:
            parser.error("--revision requires --source")
        extracted = (Path(__file__).parent / "reference/uboot-partition-fixup.c").read_text()
    if args.save_extracted:
        args.save_extracted.write_text(extracted)
    print("Extracted functions SHA256:", hashlib.sha256(extracted.encode()).hexdigest())
    old = 'partition@540000 { label = "old"; reg = <0x540000 0xfac0000>; };'
    props = '#address-cells = <1>; #size-cells = <1>;'
    boots = ('boot-areas { ' + props + 'boot0@0 { '
             'compatible = "allwinner,sun50i-h616-nand-boot0"; '
             'reg = <0 0x40000>; label = "UBI"; }; '
             'boot0@40000 { reg = <0x40000 0x40000>; label = "boot0-1"; }; };')
    partitions = 'partitions { compatible = "fixed-partitions"; ' + props + old + ' };'
    cases = []
    for order in (boots + partitions, partitions + boots):
        for prep in (0, 1):
            for unrelated in ("", 'unrelated { value = <123>; };'):
                chip = 'nand@0 { reg = <0>; ' + props + order + ' };'
                cases.append((unrelated + chip, "/nand/nand@0/partitions",
                              "/nand/nand@0/boot-areas", prep, 1, 1, 0))
    cases += [
        ('nand@0 { reg = <0>; ' + props + boots + ' };',
         "/nand/nand@0/partitions", "/nand/nand@0/boot-areas", 1, 1, 1, 0),
        ('nand@0 { reg = <0>; ' + props + boots +
         'partitions { compatible = "fixed-partitions"; ' + props + ' }; };',
         "/nand/nand@0/partitions", "/nand/nand@0/boot-areas", 0, 1, 1, 0),
        (partitions + boots, "/nand/partitions", "/nand/boot-areas", 0, 0, 0, 0),
        (boots + partitions, "/nand/partitions", "/nand/boot-areas", 0, 0, 0, 0),
        (old, "/nand", "/sentinel", 0, 0, 0, 0),
        ('nand@0 { reg = <0>; ' + props + old + ' };',
         "/nand/nand@0", "/sentinel", 0, 0, 0, 0),
        ('nand@0 { reg = <0>; ' + props + ' };',
         "/nand/nand@0", "/sentinel", 0, 0, 0, 0),
        ("", "/nand", "/sentinel", 0, 0, 0, 0),
    ]

    def boot_node(index, offset, size, extra="", compatible=True):
        compat = ('compatible = "allwinner,sun50i-h616-nand-boot0"; '
                  if compatible else 'compatible = "test,unknown"; ')
        return (f'boot0@{offset:x} {{ {compat} reg = <0x{offset:x} 0x{size:x}>; '
                f'label = "boot0-{index}"; {extra} }};')

    full = "".join(boot_node(i, i * 0x40000, 0x40000) for i in range(4))
    # Enabled/disabled, malformed, adjacent and partially overlapping regions.
    variants = [
        ("", "", 0, 0),
        (full, "", 15, 0),
        (full, 'status = "disabled";', 0, 0),
        (full, 'status = "okay";', 15, 0),
        (boot_node(0, 0, 0x40000, 'status = "disabled";') + full[len(boot_node(0, 0, 0x40000)):],
         "", 14, 0),
        (boot_node(0, 0, 0x40000, compatible=False), "", 0, 0),
        (boot_node(0, 0, 0x40000, 'status = "reserved";'), "", 0, 0),
        (boot_node(0, 0, 0), "", 0, 1),
        (boot_node(0, 0, 0x20000), "", 0, 1),
        (boot_node(0, 0x20000, 0x40000), "", 0, 1),
        (boot_node(0, 0, 0x80000), "", 0, 1),
        (boot_node(0, 0xc0000, 0x80000), "", 0, 1),
        ('boot0@0 { compatible = "allwinner,sun50i-h616-nand-boot0"; label = "bad"; };',
         "", 0, 1),
        (full.replace('reg = <0x0 0x40000>', 'reg = <0 0x40000 0 0x40000>'), "", 0, 1),
        # All native partitions removed must not fall back to a whole-chip MTD.
        (full + boot_node(4, 0x100000, 0x400000) + boot_node(5, 0x500000, 0x40000) +
         boot_node(6, 0x540000, 0xfac0000), "", 0, 1),
    ]
    for entries, status, mask, error in variants:
        for reverse in (False, True):
            areas = 'boot-areas { ' + props + status + entries + ' };'
            order = partitions + areas if reverse else areas + partitions
            cases.append(('nand@0 { reg = <0>; ' + props + order + ' };',
                          "/nand/nand@0/partitions", "/nand/nand@0/boot-areas",
                          1, 1, mask, error))
    cases.append(('nand@0 { reg = <0>; ' + props + partitions + ' };',
                  "/nand/nand@0/partitions", "/sentinel", 1, 1, 0, 0))
    cases.append(('nand@0 { reg = <0>; status = "disabled"; ' + props +
                  'boot-areas { ' + props + full + ' };' + partitions + ' };',
                  "/nand/nand@0/partitions", "/nand/nand@0/boot-areas", 1, 2, 0, 0))
    # Wide native offsets are supported; boot-area cells follow their binding.
    wide = '#address-cells = <2>; #size-cells = <2>;'
    for cell_props, error in ((props, 0), (wide, 1)):
        areas = 'boot-areas { ' + cell_props + full + ' };'
        cases.append(('nand@0 { reg = <0>; ' + wide + areas + ' };',
                      "/nand/nand@0/partitions", "/nand/nand@0/boot-areas",
                      1, 1, 15, error))
    with tempfile.TemporaryDirectory(prefix="uboot-partition-fixup-") as directory:
        work = Path(directory)
        (work / "fixup-under-test.c").write_text(extracted)
        fixture = Path(__file__).parent / "tests/uboot_partition_fixup.c"
        exe = work / "check"
        flags = (["-DHAVE_BOOT0_FIXUP=1"]
                 if "sunxi_fdt_finalize_nand_partitions(" in extracted else [])
        subprocess.run(["cc", "-g", "-O1", "-fsanitize=address,undefined", *flags,
                        "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                        "-I" + str(work), str(fixture), "-lfdt", "-o", str(exe)], check=True)
        failures = 0
        for index, (children, parent, preserved, prep, tag, mask, error) in enumerate(cases):
            dts = work / "input.dts"
            dts.write_text('/dts-v1/; / { sentinel { value = "unchanged"; }; '
                           'nand { compatible = "test,nand"; ' + props + children + ' }; };')
            dtb = work / "input.dtb"
            subprocess.run(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o",
                            str(dtb), str(dts)], check=True)
            result = subprocess.run([str(exe), str(dtb), str(work / "output.dtb"),
                                     parent, preserved, str(prep), str(tag), str(mask), str(error)],
                                    capture_output=True, text=True)
            print(f"Case {index}: {'FAIL' if result.returncode else 'PASS'}", flush=True)
            if result.returncode:
                failures += 1
                print(result.stdout + result.stderr)
        if args.input_dtb:
            parent = "/soc/nand-controller@4011000/nand@0"
            output = args.output_dtb or work / "board.dtb"
            result = subprocess.run(
                [str(exe), str(args.input_dtb), str(output), parent + "/partitions",
                 parent + "/boot-areas", "1", "1", "15", "0",
                 "allwinner,sun50i-h616-nand-controller"],
                capture_output=True, text=True)
            print("Board DT:", "FAIL" if result.returncode else "PASS")
            print(result.stdout + result.stderr)
            if result.returncode:
                failures += 1
        if args.expect_failure:
            assert failures, "negative control unexpectedly passed"
            print(f"Reproduced failure in {failures}/{len(cases)} layouts")
        else:
            assert not failures, f"{failures}/{len(cases)} layouts failed"
            print(f"PASS: {len(cases)} layouts, each fixed up twice")


if __name__ == "__main__":
    main()
