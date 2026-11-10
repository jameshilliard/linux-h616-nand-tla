#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check callback retirement, stale handles and replacement identities."""
import argparse
import hashlib
from pathlib import Path
import subprocess

import run as tlc
from mtdchar_cases import TLC
from run_core import extract


def source_check(tree):
    nvmem = (tree / "drivers/nvmem/core.c").read_text()
    concat = (tree / "drivers/mtd/mtdconcat.c").read_text()
    bodies = {}
    for name in ("concat_read", "concat_panic_write", "concat_write", "concat_writev",
                 "concat_read_oob", "concat_write_oob", "concat_erase", "concat_xxlock",
                 "concat_is_locked", "concat_sync", "concat_suspend", "concat_resume",
                 "concat_block_isbad", "concat_block_markbad", "concat_ooblayout_ecc",
                 "concat_ooblayout_free"):
        body = bodies[name] = extract(concat, name)
        assert body.index("guard(srcu_fast)") < body.index("READ_ONCE(concat->disconnected)")
        assert body.index("READ_ONCE(concat->disconnected)") < body.index("concat->subdev")
    for name in ("__nvmem_cell_read", "__nvmem_cell_entry_write"):
        body = bodies[name] = extract(nvmem, name)
        assert body.index("guard(srcu)") < body.index("READ_ONCE(cell->disconnected)")
    body = bodies["nvmem_layout_unregister"] = extract(nvmem, "nvmem_layout_unregister")
    assert body.index("WRITE_ONCE(cell->disconnected, true)") < body.index("synchronize_srcu")
    assert body.index("synchronize_srcu") < body.index("nvmem_cell_entry_put")
    assert "module_put" not in extract(nvmem, "nvmem_cell_put")
    return dict(kernel_commit=subprocess.check_output(["git", "rev-parse", "HEAD"],
                cwd=tree, text=True).strip(),
                dirty=bool(subprocess.check_output(["git", "diff", "--name-only"], cwd=tree)),
                functions={name: hashlib.sha256(body.encode()).hexdigest()
                           for name, body in bodies.items()},
                limits="Bounded ownership and callback grace-period contracts; not a C "
                       "refinement, weak-memory proof or proof of provider hardware shutdown")


def cases():
    base = dict(Readers=2, KeepHandles=True, Drain=True, CheckRetired=True, IdentityGate=True)
    result = [dict(name=f"retirement-{readers}", module="CallbackRetirement", kind="positive",
                   expected=None, constants=dict(base, Readers=readers)) for readers in (1, 2)]
    for field, invariant in (("KeepHandles", "MetadataAlive"), ("Drain", "CallbackAlive"),
                             ("CheckRetired", "CallbackAlive"), ("IdentityGate", "CallbackAlive")):
        result.append(dict(name="retirement-missing-" + field, module="CallbackRetirement",
                           kind="mutation", expected=invariant,
                           constants=dict(base, **{field: False})))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--kernel-tree", type=Path)
    args, remaining = parser.parse_known_args()
    metadata = source_check(args.kernel_tree) if args.kernel_tree else dict(
        source_check="Not run; use --kernel-tree to check current sources")
    raise SystemExit(tlc.main(remaining, cases=cases(), toolchain=TLC, metadata=metadata,
        modules={"CallbackRetirement": dict(
            properties=[],  # Bounded safety; no liveness/fairness claim.
            invariants=["TypeOK", "MetadataAlive", "CallbackAlive"],
            actions=["Acquire", "Enter", "Check", "CallbackReturn", "Exit", "Put",
                     "Disconnect", "DrainCallbacks", "ReleaseProvider", "FreeMetadata", "Rebind"])}))
