#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reintroduce historical bug patterns into generated modern test code only."""
import hashlib
import json
import argparse
from pathlib import Path
import resource
import subprocess
import tempfile
from boundary_common import HERE
from run_transport import units
from run_c import extract_function

# Semantic regressions adapted to the checked source, not historical kernels.
CASES = [
    ("pio-read-reseed", "4a5a99bc79cdc4be63933653682b0261a67a0c9f",
     "sunxi_nfc_hw_ecc_read_chunk", "\tsunxi_nfc_randomizer_config(nand, page, false);\n", "", "transport_seed()==sunxi_nfc_randomizer_state"),
    ("pio-write-reseed", "44ed1f5ff73e9e115b6f5411744d5a22ea1c855b",
     "sunxi_nfc_hw_ecc_write_chunk", "\tsunxi_nfc_randomizer_config(nand, page, false);\n", "", "transport_seed()==sunxi_nfc_randomizer_state"),
    ("dma-oob-base", "252173c69eec6998ecf1be7b11d31cd39ce92c9c",
     "sunxi_nfc_oob_read_batch", "mtd->writesize + oob_off,", "oob_off,", "got[i]==0x5b"),
    ("subpage-callback", "03b1d11a998b70df2ca699a9b9fb8ab5b0746d43",
     "sunxi_nand_hw_ecc_ctrl_init", "\tecc->write_subpage = sunxi_nfc_hw_ecc_write_subpage;\n", "", "persistent_chip.ecc.write_subpage==sunxi_nfc_hw_ecc_write_subpage"),
    ("strength-rounding", "f4c6cd1a7f2275d5bc0e494b21fff26f8dde80f0",
     "sunxi_nand_hw_ecc_ctrl_init", "\t\t\tecc->strength = strengths[ecc_mode];\n", "",
     "n.ecc.strength==c.ecc_strengths[mode]"),
    ("ecc-block-512", "f59dab8d9e8f37f7042ba05bfbab748b1312e73e",
     "sunxi_nand_hw_ecc_ctrl_init", "\t\t\tsunxi_nand->ecc.ecc_ctl |= NFC_ECC_BLOCK_512;\n", "",
     "n.ecc.ecc_ctl & NFC_ECC_BLOCK_512"),
    ("dma-prep-null", "28f3d01eca5269b01e33c1bf976d90c33b546ad9",
     "sunxi_nfc_dma_op_prepare", "if (!dmad)", "if (IS_ERR(dmad))", "d == &descriptor"),
]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source",type=Path)
    args=parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    source, hashes, src = units(args.candidate_source)
    (HERE / "results").mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix="historical-",dir=HERE/"results"))
    records=[]
    # Recheck the positive fixture here so a preexisting failure cannot count
    # as successful detection of a mutation.
    for label,commit,fn,old,new,expected in [("positive",None,None,None,None,None),*CASES]:
        directory=out/label; directory.mkdir()
        code=dict(source)
        if fn:
            filename="transport.c" if label=="dma-prep-null" else "driver.c"
            body=extract_function(code[filename],fn)
            assert old in body
            changed=body.replace(old,new,1)
            code[filename]=code[filename].replace(body,changed,1)
        for name,text in code.items(): (directory/name).write_text(text)
        cmd=["gcc","-std=gnu11","-O1","-g","-fno-strict-aliasing","-fno-strict-overflow",
             "-funsigned-char","-fsanitize=address,undefined","-fno-sanitize-recover=all","-no-pie",
             *[n for n in code if n.endswith(".c")],"-o","check"]
        build=subprocess.run(cmd,cwd=directory,text=True,capture_output=True,timeout=120)
        (directory/"build.log").write_text(build.stdout+build.stderr)
        assert build.returncode==0,build.stderr
        run=subprocess.run(["./check"],cwd=directory,text=True,capture_output=True,timeout=180)
        (directory/"check.log").write_text(run.stdout+run.stderr)
        passed=(run.returncode==0 and not run.stderr and "ALL CHECKS PASSED" in run.stdout) if not fn else (
            run.returncode==-6 and expected in run.stderr and "Assertion" in run.stderr and
            "runtime error:" not in run.stderr and "AddressSanitizer" not in run.stderr)
        records.append(dict(case=label,fix_commit=commit,kind="adapted-historical-mutation" if fn else "positive",
            passed=passed,expected_assertion=expected,
            generated_sha256={n:hashlib.sha256(t.encode()).hexdigest() for n,t in code.items()}))
        print(("PASS " if passed else "FAIL ")+label,flush=True)
    (out/"summary.json").write_text(json.dumps(dict(passed=all(r["passed"] for r in records),cases=records,
        baseline_functions=hashes,
        driver_sha256=hashlib.sha256(src["sunxi_nand.c"].encode()).hexdigest(),
        candidate=bool(args.candidate_source)),indent=2)+"\n")
    print("Logs:",out)
    return 0 if all(r["passed"] for r in records) else 1


if __name__=="__main__":
    raise SystemExit(main())
