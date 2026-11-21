#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the ordinary and firmware ECC and page-transfer policies with stable TLC."""
import sys
import tempfile
from pathlib import Path

from cases import CASES, MODULES
from mtdchar_cases import TLC  # pinned stable toolchain, shared with CI
from run import main
from run_v11 import FIRMWARE, NAND_BASE, ORDINARY, pinned_nand_base, pinned_pair

if __name__ == "__main__":
    modules = {name: MODULES[name] for name in
               ("NandEcc", "NandSubpage", "NandMarkerAdmission", "NandMarkerFallback", "NandPlainMarker",
                "NandStreamingRecovery", "NandTransfer", "NandReadRetry", "NandBatch",
                "NandSubpageProgram")}
    with tempfile.TemporaryDirectory() as directory:
        pinned_pair(Path(directory))
    pinned_nand_base()
    sources = {"ordinary_driver_sha256": ORDINARY, "deferred_driver_sha256": FIRMWARE,
               "nand_base_sha256": NAND_BASE}
    raise SystemExit(main(sys.argv[1:], cases=[c for c in CASES if c["module"] in modules],
                          modules=modules, toolchain=TLC, metadata=sources))
