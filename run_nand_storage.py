#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual NAND allocation/layout ownership helpers under ASan/UBSan."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from boundary_common import HERE, bodies, check

FILES = {"nand_lifetime.c": "drivers/mtd/nand/raw/nand_lifetime.c"}
FUNCTIONS = {"nand_lifetime.c": [
    "nand_storage_release", "nand_storage_init", "nand_storage_get", "nand_storage_put",
    "devm_nand_storage_init", "devm_nand_controller_init", "nand_mtd_get_device",
    "nand_retained_ooblayout_ecc", "nand_retained_ooblayout_free", "nand_mtd_release",
    "nand_prepare_lifetime", "nand_set_lifetime", "nand_set_shared_lifetime"]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-tree", type=Path)
    parser.add_argument("--update-reference", action="store_true")
    args = parser.parse_args()
    reference = HERE / "reference/nand-storage.json"
    if args.update_reference and not args.kernel_tree:
        parser.error("--update-reference requires --kernel-tree")
    if args.kernel_tree:
        src = {name: (args.kernel_tree / path).read_text() for name, path in FILES.items()}
        if args.update_reference:
            if subprocess.check_output(["git", "status", "--porcelain"], cwd=args.kernel_tree):
                parser.error("commit the candidate before snapshotting it")
            reference.write_text(json.dumps(dict(
                commit=subprocess.check_output(["git", "rev-parse", "HEAD"],
                    cwd=args.kernel_tree, text=True).strip(), sources=src,
                sha256={name: hashlib.sha256(text.encode()).hexdigest() for name, text in src.items()}),
                indent=2) + "\n")
    else:
        payload = json.loads(reference.read_text())
        src = payload["sources"]
        assert payload["sha256"] == {name: hashlib.sha256(text.encode()).hexdigest()
                                     for name, text in src.items()}
    code, proto, hashes = bodies(src, FUNCTIONS)
    source = src["nand_lifetime.c"]
    layout = source[source.index("static const struct mtd_ooblayout_ops nand_retained_ooblayout"):
                    source.index("static void nand_mtd_release")]
    lifetime = source[source.index("struct nand_lifetime {"):source.index("static void nand_storage_release")]
    check("nand-core-storage", {"check.c":
        (HERE / "tests/nand_storage.h").read_text() + lifetime + proto + layout + code +
        '\n#line 1 "nand_storage.c"\n' + (HERE / "tests/nand_storage.c").read_text()}, hashes,
        coverage=(src, FUNCTIONS), limits=[
            "Actual allocation ownership, name/layout snapshot, get-device and final-release bodies",
            "Heap objects freed under ASan; allocation-failure and shared-storage cases are sequential",
            "Device/node refs, locks and devres are explicit shims, not a whole-driver concurrency proof",
            "Real module unload and retained writable FDs are covered by the separate QEMU fixture"])
