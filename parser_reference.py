#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Identify the parser's reference API; this is not C execution/refinement."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

from run_core import extract

HERE = Path(__file__).resolve().parent
SOURCE = "drivers/mtd/mtdpart.c"
FUNCTION = "add_mtd_partitions"


def reference_kind(body):
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
    pairs = {
        "metadata": (r"mtd_get_data\(\s*child\s*\)", r"mtd_put_data\(\s*child\s*\)"),
        "operational": (r"get_mtd_device\(\s*child\s*,\s*-1\s*\)",
                        r"put_mtd_device\(\s*child\s*\)"),
    }
    matched = []
    for kind, (get, put) in pairs.items():
        gets, puts = list(re.finditer(get, code)), list(re.finditer(put, code))
        if gets or puts:
            if len(gets) != 1 or len(puts) != 1:
                raise ValueError("unpaired or ambiguous parser reference APIs")
            matched.append((kind, gets[0].start(), puts[0].start()))
    if len(matched) != 1:
        raise ValueError("unknown or mixed parser reference APIs")
    kind, get, put = matched[0]
    parse = code.index("parse_mtd_partitions(child,")
    unlock = code.index("mutex_unlock(&master->master.partitions_lock)", get)
    recheck = code.index("if (!child->registered)", parse)
    if not get < unlock < parse < recheck < put:
        raise ValueError("parser reference no longer encloses the unlocked parse/recheck")
    if kind == "operational" and not re.search(r"if\s*\(ret\)\s*goto err_unlock;",
                                              code[get:unlock]):
        raise ValueError("operational-open failure path changed")
    return kind


def profiles(kernel_tree=None, revision=None):
    path = HERE / "reference/partition-parser.json"
    data = json.loads(path.read_text())
    for name, item in data.items():
        if hashlib.sha256(item["body"].encode()).hexdigest() != item["sha256"]:
            raise ValueError("parser snapshot checksum mismatch: " + name)
        if reference_kind(item["body"]) != item["reference_kind"]:
            raise ValueError("parser snapshot policy mismatch: " + name)
    if kernel_tree:
        if revision:
            commit = subprocess.check_output(["git", "rev-parse", revision + "^{commit}"],
                                             cwd=kernel_tree, text=True).strip()
            source = subprocess.check_output(["git", "show", commit + ":" + SOURCE],
                                             cwd=kernel_tree, text=True)
            dirty = False
        else:
            commit = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                             cwd=kernel_tree, text=True).strip()
            source = (kernel_tree / SOURCE).read_text()
            dirty = bool(subprocess.check_output(["git", "status", "--porcelain", "--", SOURCE],
                                                 cwd=kernel_tree))
        body = extract(source, FUNCTION)
        data["current"] = dict(commit=commit, path=SOURCE, function=FUNCTION, body=body,
                               sha256=hashlib.sha256(body.encode()).hexdigest(),
                               reference_kind=reference_kind(body), dirty=dirty)
    return data


def evidence(data):
    return {name: {k: v for k, v in item.items() if k != "body"}
            for name, item in data.items()}
