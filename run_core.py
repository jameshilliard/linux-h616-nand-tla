#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute pinned NAND-core/BBT bodies against a finite, faultable NAND fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
FUNCTIONS = {
    "mtd.h": ["mtd_get_master", "mtd_oobavail"],
    "mtdcore.c": ["mtd_ooblayout_free", "mtd_ooblayout_find_region",
                  "mtd_ooblayout_get_bytes", "mtd_ooblayout_set_bytes",
                  "mtd_ooblayout_get_databytes", "mtd_ooblayout_set_databytes"],
    "nand_base.c": ["nand_bbm_get_next_page", "nand_fill_oob", "nand_do_write_oob",
                    "nand_default_block_markbad", "nand_markbad_bbm", "nand_block_markbad_lowlevel",
                    "nand_transfer_oob",
                    "nand_setup_read_retry", "nand_do_read_ops", "nand_do_read_oob",
                    "nand_write_page", "nand_do_write_ops"],
    "nand_bbt.c": ["bbt_get_entry", "bbt_mark_entry", "check_pattern_no_oob",
                   "check_pattern", "check_short_pattern", "add_marker_len",
                   "read_bbt", "read_abs_bbt", "scan_read_data", "scan_read_oob",
                   "scan_read", "scan_write_bbt", "bbt_get_ver_offs", "scan_block_fast",
                   "bbt_block_checkbad", "create_bbt", "search_bbt", "search_read_bbts",
                   "get_bbt_block", "mark_bbt_block_bad", "write_bbt", "check_create",
                   "nand_update_bbt", "mark_bbt_region", "nand_isbad_bbt", "nand_markbad_bbt"],
}


def extract(source, name):
    match = re.search(r"^[A-Za-z_][\w \t*\n]*\b" + name +
                      r"\([^;{}]*\)\n\{", source, re.M)
    if not match:
        raise ValueError("missing function " + name)
    start = match.start()
    # Top-level kernel closing braces start in column zero.
    end = source.index("\n}", match.end()) + 2
    return source[start:end].lstrip("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    pin = json.loads((HERE / "source.json").read_text())
    sources = {}
    for name, info in pin["core_sources"].items():
        data = (HERE / "reference" / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != info["sha256"]:
            parser.error("reference hash mismatch: " + name)
        sources[name] = data.decode()
    header = (HERE / "tests/core.h").read_text()
    header = header.replace("/* @bbm@ */", sources["bbm.h"])
    definitions = []
    for name in ("rawnand.h", "nand_bbt.c"):
        for m in re.finditer(r"^#define (?:NAND_|BBT_)[^\n\\]*(?:\\\n[^\n\\]*)*",
                             sources[name], re.M):
            if name == "nand_bbt.c" or re.match(
                    r"#define NAND_(?:BUSWIDTH_16|NEED_READRDY|NO_SUBPAGE_WRITE|USES_DMA|"
                    r"BBM_FIRSTPAGE|BBM_SECONDPAGE|BBM_LASTPAGE|NO_BBM_QUIRK)\s", m[0]):
                definitions.append(m[0])
    generated, hashes = [], {}
    for filename, names in FUNCTIONS.items():
        for name in names:
            body = extract(sources[filename], name)
            line = sources[filename][:sources[filename].index(body)].count("\n") + 1
            generated.append(f'#line {line} "{filename}"\n' + body)
            hashes[f"{filename}:{name}"] = hashlib.sha256(body.encode()).hexdigest()
    combined = ("\n".join(definitions) + '\n#line 1 "core.h"\n' + header +
                "\n" + "\n".join(generated) + '\n#line 1 "core.c"\n' +
                (HERE / "tests/core.c").read_text())
    (HERE / "results").mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="core-", dir=HERE / "results"))
    (out / "check.c").write_text(combined)
    command = [args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
               "-fno-strict-aliasing", "-fno-strict-overflow", "-funsigned-char",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
               "check.c", "-o", "check"]
    build = subprocess.run(command, cwd=out, text=True, capture_output=True)
    (out / "build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        print(build.stdout + build.stderr)
        return 1
    run = subprocess.run(["./check"], cwd=out, text=True, capture_output=True, timeout=180)
    (out / "check.log").write_text(run.stdout + run.stderr)
    passed = run.returncode == 0 and "ALL CORE CHECKS PASSED" in run.stdout and not run.stderr
    from source_coverage import build_and_collect
    coverage = build_and_collect(out, command, sources, FUNCTIONS) if passed else None
    if coverage:
        missed = [name for name, counts in coverage["functions"].items() if not counts["calls"]]
        if missed:
            print("FAIL unexecuted core functions:", ", ".join(missed))
            passed = False
    (out / "summary.json").write_text(json.dumps(dict(
        passed=passed, functions=hashes, output=run.stdout, diagnostics=run.stderr,
        coverage=coverage,
        source_sha256={n: i["sha256"] for n, i in pin["core_sources"].items()}), indent=2) + "\n")
    print(run.stdout + run.stderr)
    print("Logs:", out)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
