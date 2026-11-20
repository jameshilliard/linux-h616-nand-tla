#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Model the ordinary and firmware ECC read policies with stable TLC."""
import sys

from cases import CASES, MODULES
from mtdchar_cases import TLC  # pinned stable toolchain, shared with CI
from run import main

if __name__ == "__main__":
    modules = {name: MODULES[name] for name in
               ("NandEcc", "NandSubpage", "NandMarkerAdmission", "NandPlainMarker")}
    raise SystemExit(main(sys.argv[1:], cases=[c for c in CASES if c["module"] in modules],
                          modules=modules, toolchain=TLC))
