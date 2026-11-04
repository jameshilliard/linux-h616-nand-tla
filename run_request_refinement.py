#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Trace actual NAND core/page callers through DMA and interrupt recovery."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re

from boundary_common import HERE, check
from driver_boundary import fixture_body
from run_transport import units, replace_once
from codec_oracle import scramble
from oob_pipeline_cases import advance
from recovery_trace_cases import freeze
import run as tlc

MODEL=dict(invariants=["Protocol", "Snapshots", "MappingSafety", "CleanReturn",
                       "NoWriteReplay", "Results", "Balanced"], actions=["Advance"])


def trace_case(name, traces, expected=None):
    return dict(name="request-trace-"+name,module="NandRequestTrace",
                kind="mutation" if expected else "positive",expected=expected,
                constants=dict(Traces=freeze(traces)))


def verify(out):
    report=json.loads((out/"summary.json").read_text())
    assert report["passed"]
    for name,digest in report["artifacts_sha256"].items():
        assert hashlib.sha256((out/name).read_bytes()).hexdigest()==digest
    raw=vectors()
    assert (out/"request-vectors.bin").read_bytes()==raw
    observed=(out/"request-media.bin").read_bytes()
    expected=b"".join(raw[i:i+2304]*2 for i in range(0,len(raw),2304))
    assert observed==expected,"independent stored-byte comparison (opaque parity contract)"
    traces=[json.loads(line) for line in (out/"request-traces.jsonl").read_text().splitlines()]
    assert [t["id"] for t in traces]==list(range(810))
    # De-duplicate exact behavioral projections only. Account for every input
    # including successful retries and all format/backend combinations.
    groups={}
    for t in traces:
        assert t["events"][0][0]=="begin" and t["events"][-1][0]=="result"
        for e in t["events"]:
            assert len(e)==8 and isinstance(e[0],str) and all(type(v)is int for v in e[1:])
        projection={k:v for k,v in t.items() if k not in ("id","fault","format","backend")}
        key=json.dumps(projection,sort_keys=True)
        groups.setdefault(key,[]).append(t["id"])
    projections=[json.loads(k) for k in groups]
    tests=[trace_case(f"source-{i//8:03d}",projections[i:i+8]) for i in range(0,len(projections),8)]
    good=next(t for t in traces if any(e[0]=="confirm" for e in t["events"]) and
              any(e[0]=="map" for e in t["events"]))
    def mutate(name,invariant,edit):
        t=copy.deepcopy(good); edit(t); tests.append(trace_case(name,[t],invariant))
    mutate("lost-unmap","Balanced",lambda t:t["events"].__setitem__(
        next(i for i,e in enumerate(t["events"]) if e[0]=="unmap"),["cleanup",0,0,0,0,0,0,0]))
    mutate("wrong-snapshot","Snapshots",lambda t:next(e for e in t["events"] if e[0]=="map").__setitem__(3,0))
    mutate("active-unmap","MappingSafety",lambda t:next(e for e in t["events"] if e[0]=="unmap").__setitem__(5,1))
    mutate("irq-leak","CleanReturn",lambda t:t["events"][-1].__setitem__(6,2))
    mutate("ecc-leak","CleanReturn",lambda t:t["events"][-1].__setitem__(7,1))
    mutate("false-short-success","Results",lambda t:t.update(retlen=0))
    def duplicate(t):
        i=next(i for i,e in enumerate(t["events"]) if e[0]=="confirm")
        t["events"].insert(i,copy.deepcopy(t["events"][i]))
    mutate("double-confirm","NoWriteReplay",duplicate)
    meta=dict(source_sha256=report["driver_sha256"],functions=report["functions"],
              source_artifacts=report["artifacts_sha256"],
              trace_count=len(traces),projection_count=len(projections),
              projection_inputs=list(groups.values()),stored_images=len(expected)//2304)
    print(f"Request oracle: {len(traces)} traces, {len(projections)} projections, {len(expected)//2304} stored images",flush=True)
    return tests,meta


def vectors():
    vendor=(HERE/"reference/compat/ndfc_base.c").read_bytes()
    manifest=json.loads((HERE/"reference/compat/source.json").read_text())
    assert hashlib.sha256(vendor).hexdigest()==manifest["files"]["ndfc_base.c"]
    table=re.search(r"const u32 random_seed\[128\] = \{(.*?)\};",vendor.decode(),re.S)[1]
    seeds=[int(x,16) for x in re.findall(r"0x[0-9a-f]+",table)]
    result=bytearray()
    for fmt in range(3):
        for backend in range(3):
            data=bytes((i*19+(i>>8)*7+fmt)&255 for i in range(2048))
            seed=seeds[17]
            physical=b"".join(scramble(data[i:i+1024],seed) if fmt else data[i:i+1024]
                              for i in range(0,2048,1024))
            # These 2 KiB/256-byte OOB layouts are selected by the real attach
            # code: normal H616 maximizes to 32+32, vendor H616 packs 8+0.
            lengths=([8,0] if fmt==2 else [32,32]) if backend==2 else [4,4]
            spare=bytearray()
            for n in lengths:
                user=bytes([255]*n)
                # USER_DATA continues the main-data stream after 1024 bytes.
                # scramble() itself accounts for the initial 15 LFSR clocks;
                # do not apply that warm-up twice to the ECC seed.
                if fmt: user=scramble(user,advance(seed,8192))
                if fmt==1 and not spare and n>=2: user=bytes([255,255])+user[2:]
                spare+=user+bytes([0x5b]*56)
            tail=bytes([255]*(256-len(spare)))
            spare+=scramble(tail,seed) if fmt else tail
            result+=physical+spare
    return bytes(result)


def generate(candidate):
    unit,hashes,src=units(candidate)
    api=(HERE/"tests/request_api.h").read_text()
    unit["request_api.h"]=api
    driver=unit["driver.c"]
    driver='#include "request_api.h"\n'+driver
    replacements={
        "sunxi_nfc_wait_cmd_fifo_empty":"return request_fifo();",
        "sunxi_nfc_wait_events":"int ret=request_wait(events,poll,ms); if(!ret) flow.active=false; return ret;",
        "sunxi_nfc_dma_op_abort":"request_abort(); flow.active=false;",
        "sunxi_nfc_dma_op_cleanup":"request_cleanup(dir); flow.mapped=false;",
        "sunxi_nfc_dma_op_prepare":
            "int ret=request_prepare(buf,size,count,direction); sg->len=size*count; "
            "if(!ret) { dma_buf=(void *)buf; dma_steps=count; dma_size=size; flow.mapped=true; } return ret;",
    }
    for name,body in replacements.items():
        old=fixture_body(driver,name)
        driver=replace_once(driver,old,old[:old.index("{")]+"{ "+body+" }")
    driver=replace_once(driver,"flow.active=flow.mapped; advance_seed(1024); command_count++;",
                        "flow.active=flow.mapped; advance_seed(1024); command_count++; request_launch(dma); request_after_ecc();")
    # The source accessors span separately typed fixtures; hardware is shared
    # by value and never through incompatible kernel struct pointer casts.
    driver=replace_once(driver,"    int ret;\n    if(raw) {",
                        "    int ret; request_event(\"page-enter\",write,page);\n    if(raw) {")
    driver=replace_once(driver,"    operation_count++; return ret;",
                        "    request_event(\"page-exit\",ret,n->mtd.ecc_stats.failed-f); operation_count++; return ret;")
    driver=replace_once(driver,"wire_mode=0x80; program_pending=true;",
                        "request_event(\"program-begin\",0,0); wire_mode=0x80; program_pending=true;")
    driver=replace_once(driver,"case 0x10:\n        assert(program_pending",
                        "case 0x10:\n        request_event(\"confirm\",0,0);\n        assert(program_pending")
    driver=replace_once(driver,"bool fail=wire_fail && wire_count==wire_fail;",
                        "bool fail=wire_fail && wire_count==wire_fail;\n    if(fail) request_wire_error();")
    driver+= '\n#line 1 "request_driver.c"\n'+(HERE/"tests/request_driver.c").read_text()
    unit["driver.c"]=driver
    transport='#include "request_api.h"\n'+unit["transport.c"]
    transport=replace_once(transport,"static void writel(u32 v, void *p) { media_write_reg(offset(p),v); }",
        "static void request_mmio_write(u32,void *);\nstatic void writel(u32 v, void *p) { request_mmio_write(v,p); }")
    transport+= '\n#line 1 "request_transport.c"\n'+(HERE/"tests/request_transport.c").read_text()
    unit["transport.c"]=transport
    unit["check.c"]=replace_once(unit["check.c"],
        "int main(void) { composed_main(); core_retry_tests(); core_copy_tests(); wire_tests(); return 0; }",
        (HERE/"tests/request_core.c").read_text())
    # Explicit input path is copied into the result by the runner, not read
    # from external state while source execution is underway.
    return unit,hashes,src


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source",type=Path)
    parser.add_argument("--results",type=Path)
    parser.add_argument("--source-only",action="store_true")
    parser.add_argument("--case",default="")
    args=parser.parse_args()
    if args.results:
        if args.candidate_source: parser.error("--results must use its recorded source")
        tests,meta=verify(args.results)
        return tlc.main(["--case",args.case],cases=tests,modules={"NandRequestTrace":MODEL},metadata=meta)
    unit,hashes,src=generate(args.candidate_source)
    # Embed independent bytes as an input-file initializer, outside all
    # extracted kernel bodies. Sanitizer and coverage runs reset it identically.
    raw=vectors()
    initializer="static const unsigned char request_vectors[]={"+",".join(map(str,raw))+"};\n"
    unit["check.c"]=replace_once(unit["check.c"],"int main(void)\n{\n    unsigned id=0;",
        initializer+"int main(void)\n{\n    FILE *vf=fopen(\"request-vectors.bin\",\"wb\"); assert(vf); "
        "assert(fwrite(request_vectors,sizeof(request_vectors),1,vf)==1); assert(!fclose(vf));\n"
        "    unsigned id=0;")
    names={}
    for key in hashes:
        filename,name=key.split(":",1); names.setdefault(filename,[]).append(name)
    out=check("request",unit,hashes,coverage=(src,names),require_all_called=False,
        artifacts=("request-traces.jsonl","request-media.bin","request-vectors.bin"),limits=[
        "Actual core/page/ECC/OOB/command/DMA/wait/abort helpers through value adapters",
        "Synthetic 2 KiB pages; modeled DMA payload, parity, retirement and reset contracts",
        "Independent vendor-seed raw input; no physical board observation or arbitrary C refinement proof"])
    tests,meta=verify(out)
    if args.source_only: return 0
    return tlc.main(["--case",args.case],cases=tests,modules={"NandRequestTrace":MODEL},metadata=meta)


if __name__=="__main__":
    raise SystemExit(main())
