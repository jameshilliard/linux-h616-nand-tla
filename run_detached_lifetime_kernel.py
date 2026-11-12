#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test NVMEM/MTD detach in a disposable QEMU VM, never on host hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-tree", required=True, type=Path)
    parser.add_argument("--kernel-build", required=True, type=Path)
    parser.add_argument("--busybox", required=True, type=Path,
                        help="statically linked x86-64 BusyBox executable")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--accel", choices=("tcg", "kvm"), default="tcg")
    parser.add_argument("--cpu", default="max", help="QEMU CPU model (default: max)")
    parser.add_argument("--suite", choices=("nvmem", "mtd"), default="nvmem")
    parser.add_argument("--mtd-pm-only", action="store_true",
                        help="run only the MTD class-PM removal interleaving")
    args = parser.parse_args()
    tree, build = args.kernel_tree.resolve(), args.kernel_build.resolve()
    tested_commit = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                           cwd=tree, text=True).strip()
    tested_tree = subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"],
                                         cwd=tree, text=True).strip()
    tested_dirty = bool(subprocess.check_output(["git", "diff", "--name-only"], cwd=tree))
    if args.mtd_pm_only and args.suite != "mtd":
        parser.error("--mtd-pm-only requires --suite mtd")
    if args.suite == "nvmem" and "CONFIG_GPIOLIB=y\n" not in (build / ".config").read_text():
        parser.error("the NVMEM WP GPIO reprobe test requires CONFIG_GPIOLIB=y")
    if args.suite == "mtd" and "CONFIG_PM_SLEEP=y\n" not in (build / ".config").read_text():
        parser.error("the MTD PM-retirement test requires CONFIG_PM_SLEEP=y")
    source = HERE / "tests/kernel" / ("nvmem_disconnect" if args.suite == "nvmem"
                                    else "mtd_device_detach")
    out = Path(tempfile.mkdtemp(prefix=f"{args.suite}-detach-kernel-", dir=HERE / "results"))

    def run(command, log, **kwargs):
        with (out / log).open("w") as stream:
            subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                           check=True, **kwargs)

    run(["make", "-C", str(tree), f"O={build}", f"-j{args.jobs}", "W=1",
         "bzImage", "modules"], "kernel-build.log")
    run(["make", "-C", str(tree), f"O={build}", f"M={source}",
         f"MO={out / 'modules'}", f"-j{args.jobs}", "W=1", "modules"], "modules-build.log")
    root = out / "root"
    for directory in ("bin", "dev", "proc", "sys"):
        (root / directory).mkdir(parents=True)
    shutil.copy2(args.busybox, root / "bin/busybox")
    (root / "bin/sh").symlink_to("busybox")
    shutil.copyfile(source / ("init-pm" if args.mtd_pm_only else "init"), root / "init")
    (root / "init").chmod(0o755)
    for name in ("holder", "provider"):
        shutil.copy2(out / f"modules/{args.suite}_detach_{name}.ko", root)
    if args.suite == "mtd":
        shutil.copy2(build / "drivers/mtd/mtd.ko", root)
        shutil.copy2(build / "drivers/mtd/nand/nandcore.ko", root)
        shutil.copy2(build / "drivers/mtd/nand/raw/nand.ko", root)
        shutil.copy2(out / "modules/raw_nand_detach_test.ko", root)
        shutil.copy2(out / "modules/virt_concat_detach.ko", root)
        shutil.copy2(out / "modules/mtd_pm_detach_test.ko", root)
        shutil.copy2(out / "modules/mtd_partition_race_test.ko", root)
        run(["cc", "-static", "-O2", "-Wall", "-Wextra", "-Werror",
             str(source / "load_fail.c"), "-o", str(root / "load_fail")], "userspace-build.log")
        run(["cc", "-static", "-O2", "-Wall", "-Wextra", "-Werror",
             str(source / "retained_fd.c"), "-o", str(root / "retained_fd")], "retained-fd-build.log")
    names = subprocess.check_output(["find", ".", "-print0"], cwd=root)
    archive = subprocess.run(["cpio", "--null", "-o", "-H", "newc", "--owner=0:0"],
                             cwd=root, input=names, capture_output=True, check=True).stdout
    (out / "initramfs.cpio").write_bytes(archive)
    run(["qemu-system-x86_64", "-machine", f"accel={args.accel}", "-cpu", args.cpu, "-m", "2048",
         "-smp", "4", "-kernel", str(build / "arch/x86/boot/bzImage"),
         "-initrd", str(out / "initramfs.cpio"),
         "-append", "console=ttyS0 panic=-1 oops=panic", "-nographic", "-no-reboot",
         "-nic", "none"], "qemu.log", timeout=args.timeout)
    log = (out / "qemu.log").read_text()
    if args.mtd_pm_only:
        markers = ["MTD-PM-DETACH: active callback drained and later callbacks blocked PASS",
                   "MTD-PM-DETACH-QEMU: PASS"]
    elif args.suite == "nvmem":
        markers = [f"NVMEM-DETACH-QEMU: phase {phase} PASS" for phase in (1, 2, 3)]
        markers += ["NVMEM-DETACH-QEMU: PASS"]
        markers += ["NVMEM-DETACH-QEMU: layout unbind and rebind PASS"]
        markers += ["NVMEM-DETACH-QEMU: legacy provider owner preserved PASS"]
        markers += ["NVMEM-DETACH-QEMU: registration rollback releases GPIO PASS"]
        markers += ["NVMEM-DETACH-QEMU: sysfs registration failure and notifications PASS"]
        markers += ["NVMEM-DETACH: explicit NULL owner and legacy fallback PASS"]
    else:
        markers = ["MTD-DETACH: provider metadata freed", "MTD-DETACH: provider unloaded",
                   "MTD-DETACH: final device puts after core unload",
                   "MTD-DETACH-QEMU: nested handles and module unload PASS",
                   "MTD-DETACH-QEMU: initialization rollback PASS",
                   "MTD-PM-DETACH: active callback drained and later callbacks blocked PASS",
                   "concat-lifetime: PASS",
                   "MTD-DETACH-QEMU: raw NAND controller unload with retained FD PASS",
                   "MTD-DETACH-QEMU: NVMEM pins NAND core, not controller PASS",
                   "MTD-PARTITION-RACE: parser survives deletion; orphan children rejected PASS",
                   "raw-nand-lifetime: PASS", "MTD-DETACH-QEMU: PASS"]
        markers += [f"concat-oob: mask {mask}, {kind} masters: PASS"
                    for kind in ("managed", "unmanaged") for mask in range(4)]
    # The deliberate duplicate-name failure calls dump_stack(), not WARN().
    # Exempt only its Call Trace header; still reject every BUG/WARNING and
    # unrelated stack trace, even inside this expected-error test.
    scan, expected_traces = re.subn(
        r"(sysfs: cannot create duplicate filename "
        r"'/devices/nvmem-detach-provider/nvmem-detach-fixture/cells/token@0,0'\n"
        r"CPU:[^\n]*\nTainted:[^\n]*\n)Call Trace:",
        r"\1Expected duplicate-cell trace:", log)
    bad = re.findall(r".*(?:BUG:|WARNING:|: FAIL|Call Trace:|Kernel panic|"
                     r"possible circular locking|possible recursive locking).*", scan)
    passed = all(marker in log for marker in markers) and not bad
    (out / "summary.json").write_text(json.dumps(dict(
        passed=passed, markers={m: m in log for m in markers}, diagnostics=bad,
        expected_duplicate_cell_traces=expected_traces,
        kernel_commit=tested_commit, kernel_tree=tested_tree, kernel_dirty=tested_dirty,
        kernel_config_sha256=hashlib.sha256((build / ".config").read_bytes()).hexdigest(),
        kernel_config=[line for line in (build / ".config").read_text().splitlines()
                       if re.match(r"CONFIG_(?:MTD(?:_.*)?|NVMEM(?:_.*)?|KASAN(?:_.*)?|"
                                   r"PM(?:_.*)?|SUSPEND|PROVE_LOCKING|DEBUG_KOBJECT_RELEASE)=", line)],
        suite=args.suite,
        qemu_cpu=args.cpu,
        mtd_pm_only=args.mtd_pm_only,
        scope="Synthetic callback draining and retained device/consumer references; "
              "not a complete MTD redesign"), indent=2) + "\n")
    print("PASS" if passed else "FAIL", out)
    print("\n".join(line for line in log.splitlines() if "-DETACH" in line))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
