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
    parser.add_argument("--suite", choices=("nvmem", "mtd"), default="nvmem")
    args = parser.parse_args()
    tree, build = args.kernel_tree.resolve(), args.kernel_build.resolve()
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
    shutil.copyfile(source / "init", root / "init")
    (root / "init").chmod(0o755)
    for name in ("holder", "provider"):
        shutil.copy2(out / f"modules/{args.suite}_detach_{name}.ko", root)
    if args.suite == "mtd":
        shutil.copy2(build / "drivers/mtd/mtd.ko", root)
        shutil.copy2(build / "drivers/mtd/nand/nandcore.ko", root)
        shutil.copy2(build / "drivers/mtd/nand/raw/nand.ko", root)
        shutil.copy2(out / "modules/raw_nand_detach_test.ko", root)
        shutil.copy2(out / "modules/virt_concat_detach.ko", root)
        run(["cc", "-static", "-O2", "-Wall", "-Wextra", "-Werror",
             str(source / "load_fail.c"), "-o", str(root / "load_fail")], "userspace-build.log")
    names = subprocess.check_output(["find", ".", "-print0"], cwd=root)
    archive = subprocess.run(["cpio", "--null", "-o", "-H", "newc", "--owner=0:0"],
                             cwd=root, input=names, capture_output=True, check=True).stdout
    (out / "initramfs.cpio").write_bytes(archive)
    run(["qemu-system-x86_64", "-machine", f"accel={args.accel}", "-cpu", "max", "-m", "2048",
         "-smp", "4", "-kernel", str(build / "arch/x86/boot/bzImage"),
         "-initrd", str(out / "initramfs.cpio"),
         "-append", "console=ttyS0 panic=-1 oops=panic", "-nographic", "-no-reboot",
         "-nic", "none"], "qemu.log", timeout=args.timeout)
    log = (out / "qemu.log").read_text()
    if args.suite == "nvmem":
        markers = [f"NVMEM-DETACH-QEMU: phase {phase} PASS" for phase in (1, 2, 3)]
        markers += ["NVMEM-DETACH-QEMU: PASS"]
        markers += ["NVMEM-DETACH-QEMU: layout unbind and rebind PASS"]
    else:
        markers = ["MTD-DETACH: provider metadata freed", "MTD-DETACH: provider unloaded",
                   "MTD-DETACH: final device puts after core unload",
                   "MTD-DETACH-QEMU: nested handles and module unload PASS",
                   "MTD-DETACH-QEMU: initialization rollback PASS",
                   "concat-lifetime: PASS",
                   "raw-nand-lifetime: PASS", "MTD-DETACH-QEMU: PASS"]
    bad = re.findall(r".*(?:BUG:|WARNING:|: FAIL|Call Trace:|Kernel panic|"
                     r"possible circular locking|possible recursive locking).*", log)
    passed = all(marker in log for marker in markers) and not bad
    (out / "summary.json").write_text(json.dumps(dict(
        passed=passed, markers={m: m in log for m in markers}, diagnostics=bad,
        kernel_commit=subprocess.check_output(["git", "rev-parse", "HEAD"],
                                               cwd=tree, text=True).strip(),
        kernel_dirty=bool(subprocess.check_output(["git", "diff", "--name-only"], cwd=tree)),
        kernel_config_sha256=hashlib.sha256((build / ".config").read_bytes()).hexdigest(),
        kernel_config=[line for line in (build / ".config").read_text().splitlines()
                       if re.match(r"CONFIG_(?:MTD(?:_.*)?|NVMEM(?:_.*)?|KASAN(?:_.*)?|"
                                   r"PROVE_LOCKING|DEBUG_KOBJECT_RELEASE)=", line)],
        suite=args.suite,
        scope="Synthetic callback draining and retained device/consumer references; "
              "not a complete MTD redesign"), indent=2) + "\n")
    print("PASS" if passed else "FAIL", out)
    print("\n".join(line for line in log.splitlines() if "-DETACH" in line))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
