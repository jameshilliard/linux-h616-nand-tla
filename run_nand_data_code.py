#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check synchronous NAND cleanup and late core-only storage release."""
import argparse
import hashlib
from pathlib import Path
import subprocess

import run as tlc
from mtdchar_cases import TLC
from run_core import extract


def source_check(tree):
    lifetime = (tree / "drivers/mtd/nand/raw/nand_lifetime.c").read_text()
    base = (tree / "drivers/mtd/nand/raw/nand_base.c").read_text()
    core = (tree / "drivers/mtd/mtdcore.c").read_text()
    funcs = {}
    for source, names in ((lifetime, ("nand_mtd_release", "nand_storage_release",
                                    "nand_prepare_lifetime", "nand_retained_ooblayout_ecc",
                                    "nand_retained_ooblayout_free")),
                          (base, ("nand_get_device", "nand_disconnect", "nand_block_isreserved",
                                  "nand_block_markbad", "nand_erase_nand")),
                          (core, ("mtd_put_master", "mtd_master_release"))):
        funcs.update({name: extract(source, name) for name in names})
    assert "nand_cleanup" not in funcs["nand_mtd_release"]
    assert "->release(" not in funcs["nand_mtd_release"]
    assert "kfree(storage->allocation)" in funcs["nand_storage_release"]
    prepare = funcs["nand_prepare_lifetime"]
    assert "mtd->owner = THIS_MODULE" in prepare
    assert "mtd_set_ooblayout(mtd, &nand_retained_ooblayout)" in prepare
    get = funcs["nand_get_device"]
    assert get.index("chip->disconnected") < get.index("chip->controller->lock")
    disconnect = funcs["nand_disconnect"]
    assert disconnect.index("WRITE_ONCE(chip->disconnected, true)") < disconnect.index("wake_up_all")
    assert disconnect.index("wake_up_all") < disconnect.index("chip->markbad_lock")
    assert disconnect.index("chip->markbad_lock") < disconnect.index("nand_cleanup")
    markbad = funcs["nand_block_markbad"]
    assert markbad.index("chip->markbad_lock") < markbad.index("nand_block_isbad")
    reserved = funcs["nand_block_isreserved"]
    assert reserved.index("guard(mutex)") < reserved.index("chip->disconnected")
    assert reserved.index("chip->disconnected") < reserved.index("chip->bbt")
    erase = funcs["nand_erase_nand"]
    assert erase.index("nand_get_device") < erase.index("nand_region_is_secured")
    assert "try_module_get" not in funcs["mtd_put_master"]
    assert "wait_for_completion" not in funcs["mtd_put_master"]
    return dict(kernel_commit=subprocess.check_output(["git", "rev-parse", "HEAD"],
                cwd=tree, text=True).strip(),
                dirty=bool(subprocess.check_output(["git", "diff", "--name-only"], cwd=tree)),
                functions={name: hashlib.sha256(body.encode()).hexdigest()
                           for name, body in funcs.items()},
                limits="Structural correspondence only. Ordinary MTD references retain the "
                       "NAND core; mutex/refcount/waitqueue contracts are assumed.")


def cases():
    base = dict(Users=2, Suspended=False, Drain=True, DrainMarkbad=True,
                CacheLayout=True, CoreRelease=True, Wake=True)
    result = [dict(name=f"nand-data-code-{users}-{int(suspended)}", module="NandDataCodeLifetime",
                   kind="positive", expected=None,
                   constants=dict(base, Users=users, Suspended=suspended))
              for users in (1, 2) for suspended in (False, True)]
    for field, expected in (("Drain", "AuxiliaryAlive"), ("DrainMarkbad", "AuxiliaryAlive"),
                            ("CacheLayout", "AuxiliaryAlive"), ("CoreRelease", "CodeResident")):
        changes = {field: False}
        result.append(dict(name="nand-data-code-missing-" + field, module="NandDataCodeLifetime",
                           kind="mutation", expected=expected, constants=dict(base, **changes)))
    result.append(dict(name="nand-data-code-missing-Wake", module="NandDataCodeLifetime",
                       kind="mutation", expected="Terminates", temporal=True,
                       constants=dict(base, Suspended=True, Wake=False)))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--kernel-tree", type=Path)
    args, remaining = parser.parse_known_args()
    metadata = source_check(args.kernel_tree) if args.kernel_tree else dict(
        source_check="Not run; use --kernel-tree to check current sources")
    raise SystemExit(tlc.main(remaining, cases=cases(), toolchain=TLC, metadata=metadata,
        modules={"NandDataCodeLifetime": dict(properties=["Terminates"],
            invariants=["CodeResident", "StorageAlive", "AuxiliaryAlive", "HardwareAlive"],
            actions=["Start", "ChipLock", "Check", "WakeWaiter", "BBM", "BBT", "Layout", "Put",
                     "Unpublish", "Disconnect", "Cleanup", "Teardown", "Unload", "FinalPut"])}))
