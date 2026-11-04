#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check independent probe fixes and their composition without switching branches."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
DRIVER = "drivers/mtd/nand/raw/sunxi_nand.c"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-dir", type=Path, required=True)
    parser.add_argument("--base-ref", required=True)
    parser.add_argument("--available-ref", required=True)
    parser.add_argument("--publication-ref", required=True)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()

    def git(*argv, env=None, data=None):
        return subprocess.check_output(["git", *argv], cwd=args.kernel_dir,
                                       env=env, input=data)

    commits = {name: git("rev-parse", "--verify", "--end-of-options",
                        ref + "^{commit}").decode().strip()
               for name, ref in (("base", args.base_ref),
                                 ("available", args.available_ref),
                                 ("publication", args.publication_ref))}
    sources = {name: git("show", commit + ":" + DRIVER).decode()
               for name, commit in commits.items()}
    patches = {}
    for name in ("available", "publication"):
        git("merge-base", "--is-ancestor", commits["base"], commits[name])
        paths = git("diff", "--name-only", commits["base"], commits[name]).decode().splitlines()
        if paths != [DRIVER]:
            parser.error("candidate must modify only " + DRIVER + ": " + name)
        patches[name] = git("diff", commits["base"], commits[name], "--", DRIVER)

    (HERE / "results").mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="probe-fixes-", dir=HERE / "results"))
    trees = []
    # Separate temporary indexes leave both the user's checkout and real index
    # untouched. Applying the actual diffs checks independence in both orders.
    for number, order in enumerate((("available", "publication"),
                                    ("publication", "available"))):
        env = dict(os.environ, GIT_INDEX_FILE=str(out / f"index-{number}"))
        git("read-tree", commits["base"], env=env)
        for name in order:
            git("apply", "--cached", "-", env=env, data=patches[name])
        trees.append(git("write-tree", env=env).decode().strip())
    assert trees[0] == trees[1], "patch order changed the combined tree"
    sources["combined"] = git("show", trees[0] + ":" + DRIVER).decode()

    available = ["--expect-available-children"]
    deferred = ["--expect-deferred-publication"]
    tests = [("base", sources["base"], [], None),
             ("available", sources["available"], available, None),
             ("publication", sources["publication"], deferred, None),
             ("combined", sources["combined"], available + deferred, None)]

    def replace_once(source, old, new):
        assert source.count(old) == 1, "mutation target changed: " + old
        return source.replace(old, new, 1)

    # Require specific assertion failures, not just a nonzero exit or a build
    # error. Positive cases above use the identical fixture and compiler.
    tests += [
        ("unfiltered-mutation", replace_once(sources["available"],
            "for_each_available_child_of_node_scoped(np, nand_np)",
            "for_each_child_of_node_scoped(np, nand_np)"), available,
            "!ret && registered==1 && detects==1"),
        ("early-publication-control", sources["base"], deferred,
            "scanned==expected_scans"),
        ("unregister-unpublished-mutation", replace_once(sources["publication"],
            "if (sunxi_nand->registered)", "if (true)"), deferred,
            "mtd_to_nand(m)->fixture_registered && registered"),
        ("missing-registration-state-mutation", replace_once(sources["publication"],
            "sunxi_nand->registered = true;", "sunxi_nand->registered = false;"), deferred,
            "!registered && !dma_live && !reset_deasserted"),
    ]

    records = []
    for name, source, flags, expected in tests:
        directory = out / name
        directory.mkdir()
        path = directory / "sunxi_nand.c"
        path.write_text(source)
        command = [sys.executable, str(HERE / "run_lifecycle.py"), "--cc", args.cc,
                   "--candidate-source", str(path), *flags]
        run = subprocess.run(command, text=True, capture_output=True, timeout=180)
        log = run.stdout + run.stderr
        (directory / "check.log").write_text(log)
        if expected:
            matched = (run.returncode == 1 and expected in log and "FAIL " in log and
                       "runtime error:" not in log and "AddressSanitizer" not in log and
                       "error:" not in log and "Traceback" not in log)
        else:
            matched = (run.returncode == 0 and "ALL LIFECYCLE CHECKS PASSED" in log and
                       not run.stderr)
        records.append(dict(case=name, expected_assertion=expected, matched=matched,
                            driver_sha256=hashlib.sha256(source.encode()).hexdigest()))
        print(f"{'PASS' if matched else 'FAIL'} {name}", flush=True)
        if not matched:
            print(log, flush=True)
    passed = all(record["matched"] for record in records)
    (out / "summary.json").write_text(json.dumps(dict(passed=passed, commits=commits,
        identical_composed_trees=trees[0] == trees[1], composed_tree=trees[0],
        results=records), indent=2) + "\n")
    print("Logs:", out)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
