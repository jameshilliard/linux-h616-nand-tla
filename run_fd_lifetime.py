#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Follow real character-device open/close through NAND locking and cleanup."""
import argparse
import copy
import json
import hashlib
from pathlib import Path
from boundary_common import HERE,sources,bodies,check
from run_lifetime_boundary import FUNCTIONS
from run_transport import replace_once
from recovery_trace_cases import freeze
import run as tlc

MODEL=dict(invariants=["Protocol","Snapshots","ReferenceBalance","Released"],actions=["Advance"])


def verify(out):
    report=json.loads((out/"summary.json").read_text())
    raw=(out/"fd-traces.jsonl").read_bytes()
    assert report["passed"] and hashlib.sha256(raw).hexdigest()==report["artifacts_sha256"]["fd-traces.jsonl"]
    traces=[]
    for line in raw.splitlines():
        item=json.loads(line)
        if isinstance(item,dict): traces.append(dict(item,events=[]))
        else: traces[-1]["events"].append(item)
    assert len(traces)==16 and {t["case"] for t in traces}==set(range(16))
    def case(name,ts,expected=None,kind="positive"):
        return dict(name="fd-trace-"+name,module="NandFdTrace",kind=kind,expected=expected,
                    constants=dict(Traces=freeze(ts)))
    tests=[case("source",traces)]
    for t in traces:
        if t["write"] and not t["closeFirst"]:
            tests.append(case("close-after-cleanup-"+str(t["case"]),[t],
                              "NoLockAfterCleanup","known-bug"))
    for name,expected,index in (("lost-ref","Snapshots",2),("lost-resource-release","Snapshots",5),
                                ("hidden-close-access","Snapshots",6)):
        t=copy.deepcopy(next(t for t in traces if t["write"] and not t["closeFirst"]))
        event=next(e for e in t["events"] if e[0]==("cleanup" if index==5 else "close") and (index!=6 or e[6]))
        event[index]+=1
        tests.append(case(name,[t],expected,"mutation"))
    return tlc.main([],cases=tests,modules={"NandFdTrace":MODEL},metadata=dict(
        source_sha256=report["driver_sha256"],functions=report["functions"],
        artifacts=report["artifacts_sha256"],trace_count=len(traces)))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source",type=Path)
    parser.add_argument("--results",type=Path)
    args=parser.parse_args()
    if args.results:
        if args.candidate_source: parser.error("--results uses its recorded source")
        return verify(args.results)
    src=sources(args.candidate_source)
    functions=copy.deepcopy(FUNCTIONS)
    functions["mtd.h"] += ["mtd_sync"]
    functions["mtdcore.c"] += ["put_mtd_device"]
    functions["mtdchar.c"]=["mtdchar_open","mtdchar_close"]
    functions["nand_base.c"]=["nand_get_device","nand_release_device","nand_sync"]
    code,proto,hashes=bodies(src,functions)
    header=(HERE/"tests/lifetime_boundary.h").read_text()
    header=replace_once(header,"    const char *name;",
        "    unsigned type,flags; void (*_sync)(struct mtd_info *);\n    const char *name;")
    header=replace_once(header,"struct nand_chip { struct mtd_info mtd; bool resources; };",
        "struct sunxi_nfc; struct nand_chip { struct mtd_info mtd; bool resources,suspended; "
        "struct sunxi_nfc *controller; struct lock lock; int resume_wq; };")
    header=replace_once(header,"struct sunxi_nfc { struct list_head chips; };",
        "struct sunxi_nfc { struct list_head chips; struct lock lock; };")
    unit=(HERE/"tests/boundary_list.h").read_text()+header+(
        HERE/"tests/fd_lifetime.h").read_text()+proto+"\n"+code+(
        '\n#line 1 "fd_lifetime.c"\n')+(HERE/"tests/fd_lifetime.c").read_text()
    out=check("fd-lifetime",{"check.c":unit},hashes,coverage=(src,functions),
        require_all_called=False,artifacts=("fd-traces.jsonl",),limits=[
            "Real mtdchar open/close, MTD get/put and nand_sync/get/release lock paths",
            "Retained host object storage with invalid-resource observations; not real kernel UAF",
            "Device-core/sysfs/devres ownership boundary remains explicit; no implemented lifetime fix"])
    print("FD observations:",out/"fd-traces.jsonl")
    return verify(out)


if __name__=="__main__":
    raise SystemExit(main())
