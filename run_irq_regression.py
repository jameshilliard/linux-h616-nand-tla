#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check current sun6i IRQ bank dispatch using unmodified source functions."""
import argparse
import hashlib
import re
import subprocess
from pathlib import Path

from boundary_common import HERE, bodies, check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-dir", type=Path, required=True)
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--expect-bug", action="store_true")
    args = parser.parse_args()
    revision = subprocess.check_output(
        ["git", "-C", str(args.kernel_dir), "rev-parse", args.ref + "^{commit}"], text=True).strip()
    source = subprocess.check_output(
        ["git", "-C", str(args.kernel_dir), "show", revision + ":drivers/dma/sun6i-dma.c"], text=True)
    functions = {"sun6i-dma.c": ["sun6i_read_irq_stat", "sun6i_write_irq_stat", "sun6i_dma_interrupt"]}
    code, proto, hashes = bodies({"sun6i-dma.c": source}, functions)
    definitions = "\n".join(re.findall(r"^#define DMA_IRQ_[^\n]+", source, re.M))
    hashes["source"] = hashlib.sha256(source.encode()).hexdigest()
    hashes["git_commit"] = revision
    check("sun6i-irq-regression", {"check.c":
        f"#define EXPECT_BUG {int(args.expect_bug)}\n" +
        (HERE / "tests/boundary_list.h").read_text() + definitions + "\n" +
        (HERE / "tests/irq_regression.h").read_text() + proto + "\n" + code + "\n" +
        (HERE / "tests/irq_regression.c").read_text()}, hashes,
        limits=["Exact IRQ handler and register accessors; callback/MMIO fixtures, not hardware or SMP validation"])


if __name__ == "__main__":
    main()
