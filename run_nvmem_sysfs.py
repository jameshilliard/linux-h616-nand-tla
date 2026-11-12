#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real NVMEM cell-attribute allocation paths with faultable API shims."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from run_core import extract

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-tree", required=True, type=Path)
    parser.add_argument("--revision")
    parser.add_argument("--expect-bugs", action="store_true")
    args = parser.parse_args()
    path = "drivers/nvmem/core.c"
    source = (subprocess.check_output(["git", "show", f"{args.revision}:{path}"],
                                     cwd=args.kernel_tree, text=True)
              if args.revision else (args.kernel_tree / path).read_text())
    names = ["nvmem_populate_sysfs_cells"]
    if not args.expect_bugs:
        names = ["nvmem_sysfs_free_cell_attrs", "__nvmem_sysfs_remove_cells",
                 "nvmem_sysfs_remove_cells"] + names
    bodies = {name: extract(source, name) for name in names}
    out = Path(tempfile.mkdtemp(prefix="nvmem-sysfs-", dir=HERE / "results"))
    generated = f"#define EXPECT_BUGS {int(args.expect_bugs)}\n"
    generated += (HERE / "tests/nvmem_sysfs.h").read_text()
    generated += "\n" + "\n\n".join(bodies.values())
    generated += "\n" + (HERE / "tests/nvmem_sysfs.c").read_text()
    (out / "check.c").write_text(generated)
    command = ["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
               str(out / "check.c"), "-o", str(out / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(out / "check")], capture_output=True, text=True)
    (out / "run.log").write_text(result.stdout + result.stderr)
    metadata = dict(passed=result.returncode == 0, negative_control=args.expect_bugs,
                    revision=args.revision, functions={name: hashlib.sha256(body.encode()).hexdigest()
                    for name, body in bodies.items()},
                    limits="Extracted allocation/cleanup functions; sysfs drain, locks and cell refs "
                           "are explicit shims, not full concurrency or hardware verification.")
    (out / "summary.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(result.stdout, end="")
    print(result.stderr, end="")
    print(out)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
