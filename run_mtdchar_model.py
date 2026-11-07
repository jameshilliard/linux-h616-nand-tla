#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explore MEMREAD/MEMWRITE page chunking and lifecycle contracts with TLC."""
import sys
from mtdchar_cases import MODEL, cases
import run as tlc

if __name__ == "__main__":
    raise SystemExit(tlc.main(sys.argv[1:], cases=cases(),
                             modules={"NandMtdCharChunks": MODEL}))
