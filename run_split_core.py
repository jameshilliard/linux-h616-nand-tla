#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check independent NAND-core fixes from Git refs without changing checkouts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
FIXES = ("read-retry-oob", "bbt-scan", "bbt-read", "timing-recovery")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-dir", required=True, type=Path)
    parser.add_argument("--ref", action="append", required=True, metavar="FIX=GIT_REF",
                        help="one of " + ", ".join(FIXES) + " and its independent branch/ref")
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    refs = {}
    for item in args.ref:
        fix, separator, ref = item.partition("=")
        if not separator or fix not in FIXES or not ref or fix in refs:
            parser.error("invalid or repeated --ref: " + item)
        refs[fix] = ref
    (HERE / "results").mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="split-core-", dir=HERE / "results"))
    records = []
    for fix, ref in refs.items():
        commit = subprocess.check_output(
            ["git", "rev-parse", "--verify", ref + "^{commit}"],
            cwd=args.kernel_dir, text=True).strip()
        directory = out / fix
        directory.mkdir()
        hashes = {}
        for name in ("nand_base.c", "nand_bbt.c"):
            data = subprocess.check_output(
                ["git", "show", commit + ":drivers/mtd/nand/raw/" + name], cwd=args.kernel_dir)
            (directory / name).write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        if fix == "timing-recovery":
            command = [sys.executable, str(HERE / "run_lifecycle.py"), "--cc", args.cc,
                       "--candidate-nand-base", str(directory / "nand_base.c")]
        else:
            command = [sys.executable, str(HERE / "run_core.py"), "--cc", args.cc,
                       "--candidate-dir", str(directory), "--fix", fix]
        run = subprocess.run(command, text=True, capture_output=True, timeout=180)
        (directory / "check.log").write_text(run.stdout + run.stderr)
        passed = run.returncode == 0 and "CHECKS PASSED" in run.stdout and not run.stderr
        records.append(dict(fix=fix, commit=commit, source_sha256=hashes, passed=passed))
        print(f"{'PASS' if passed else 'FAIL'} {fix} ({commit[:12]})", flush=True)
        if not passed:
            print(run.stdout + run.stderr, flush=True)
    passed = all(record["passed"] for record in records)
    (out / "summary.json").write_text(json.dumps(dict(passed=passed, results=records), indent=2) + "\n")
    print("Logs:", out)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
