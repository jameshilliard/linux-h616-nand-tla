#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Connect source-executed randomized OOB/ECC stages to independent TLC checks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import run as tlc
from oob_pipeline_cases import MODEL, case, controls, inputs, pack_vectors

HERE=Path(__file__).resolve().parent


def check_bytes(t,o):
    """Whole-buffer reference, independent of the C coalescing/capture loops."""
    user=t["user"]
    n=len(user)
    assert o["user"] == user
    offsets=[]
    protected=[]
    off=0
    prefix=[]
    for length in user:
        offsets.append(off)
        protected+=list(range(off,off+length))
        prefix += [t["decoded"][off+j]^t["eccMask"][j] for j in range(length)]
        off+=length+t["parity"]
    tail_start=off
    prefix=(prefix+[255]*10)[:10]
    if t["variable"]:
        erased=prefix[0]==255 and prefix.count(255)>=9
    else:
        special=t["step"]==1024 and (t["page"]%t["ppb"]==0 or t["page"]%t["ppb"]%128==127)
        erased=prefix[:8]==[255]*8 or special and prefix[:4]==[255]*4
    assert o["erased"]==erased,(t["id"],"spare classification")
    memory=[165]*len(t["raw"])
    for i in protected: memory[i]=t["decoded"][i]
    assert o["captured"]==memory,(t["id"],"decoded capture")
    zeros=all(k in (1,4) for k in t["kinds"])
    exceptions=sum(k!=0 for k in t["kinds"])
    blank=not zeros and exceptions and erased
    stats=[7,3]
    ret=0
    wanted_reads=[]
    if zeros or blank:
        memory=[0 if zeros else 255]*len(memory)
        stats[1]+=n if zeros else 0
    else:
        if t["want"]:
            remaining=sorted(set(range(len(memory)))-set(protected))
            groups=[]
            for i in remaining:
                if groups and i==groups[-1][-1]+1: groups[-1].append(i)
                else: groups.append([i])
            for index,region in enumerate(groups,1):
                ret=-110 if index==t["fail"] else 0
                wanted_reads.append(dict(off=region[0],dest=region[0],len=len(region),ret=ret))
                if ret: break
                for i in region: memory[i]=t["raw"][i]
            if not ret:
                if not t["dma"]:
                    for i in remaining:
                        if i<tail_start: memory[i]^=t["parityMask"][i]
                for i,mask in enumerate(t["tailMask"],tail_start): memory[i]^=mask
        if not ret:
            counts=[i+1 for i,k in enumerate(t["kinds"]) if not k]
            stats[0]+=sum(counts)
            stats[1]+=exceptions
            ret=max(counts,default=0)
    assert o["reads"]==wanted_reads,(t["id"],"raw access plan",o["reads"],wanted_reads)
    assert o["oob"]==memory,(t["id"],"OOB bytes")
    assert o["stats"]==stats and o["ret"]==ret,(t["id"],"result/stats")
    assert o["main"]==[not(zeros or blank),zeros,bool(blank)] and o["guards"]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source",type=Path)
    parser.add_argument("--results",type=Path,help="reuse a successful source run from this script")
    parser.add_argument("--timeout",type=int,default=180)
    parser.add_argument("--download",action="store_true")
    parser.add_argument("--case",default="")
    parser.add_argument("--source-only",action="store_true",help="record/check C bytes without claiming a TLC run")
    args=parser.parse_args()
    if args.results and args.candidate_source:
        parser.error("--results reuses its recorded source; do not combine with --candidate-source")
    traces=inputs()
    expected_vectors=pack_vectors(traces)
    if args.results:
        out=args.results
        report=json.loads((out/"source-summary.json").read_text())
        raw=(out/"oob-traces.jsonl").read_bytes()
        if not report["passed"] or hashlib.sha256(raw).hexdigest()!=report["oob_traces_sha256"]:
            parser.error("source run failed or observations changed")
        vectors=(out/"vectors.bin").read_bytes()
        metadata=json.loads((out/"inputs.json").read_text())
        assert metadata==traces,"input generator differs from recorded source run"
    else:
        (HERE/"results").mkdir(exist_ok=True)
        out=Path(tempfile.mkdtemp(prefix="oob-pipeline-",dir=HERE/"results"))
        (out/"vectors.bin").write_bytes(expected_vectors)
        vectors=(out/"vectors.bin").read_bytes()
        (out/"inputs.json").write_text(json.dumps(traces,indent=2)+"\n")
        command=[sys.executable,str(HERE/"run_c.py"),"--oob-vectors",str(out/"vectors.bin")]
        if args.candidate_source:
            # Snapshot input bytes before any compiler/source extraction work.
            source=args.candidate_source.read_bytes()
            (out/"candidate-sunxi_nand.c").write_bytes(source)
            command += ["--candidate-source",str(out/"candidate-sunxi_nand.c")]
        run=subprocess.run(command,text=True,capture_output=True,timeout=300)
        (out/"driver.log").write_text(run.stdout+run.stderr)
        print(run.stdout+run.stderr,flush=True)
        if run.returncode:
            print("SOURCE CHECK FAILED; logs:",out)
            return 1
        result=Path(run.stdout.strip().split("Logs: ")[-1])
        report=json.loads((result/"summary.json").read_text())
        assert report["passed"]
        raw=(result/"oob-traces.jsonl").read_bytes()
        assert hashlib.sha256(raw).hexdigest()==report["oob_traces_sha256"]
        (out/"source-summary.json").write_text(json.dumps(report,indent=2)+"\n")
        (out/"oob-traces.jsonl").write_bytes(raw)
    if vectors!=expected_vectors or report.get("oob_vectors_sha256")!=hashlib.sha256(vectors).hexdigest():
        parser.error("source inputs are unverified or differ from the independent vector generator")
    observations=[json.loads(line) for line in raw.splitlines()]
    assert len(observations)==report["oob_trace_count"]==len(traces)
    for t,o in zip(traces,observations):
        assert o["id"]==t["id"]
        check_bytes(t,o)
        t["observed"]=o
    meta=dict(source_sha256=report["driver_sha256"],functions=report["functions"],
              fixture_sha256=report["fixture_sha256"],traces_sha256=hashlib.sha256(raw).hexdigest(),
              vectors_sha256=hashlib.sha256(vectors).hexdigest(),trace_count=len(traces),
              raw_calls=sum(len(o["reads"]) for o in observations),
              transport_failures=sum(o["ret"]<0 for o in observations),
              all_zero_results=sum(o["main"][1] for o in observations),
              erased_results=sum(o["main"][2] for o in observations))
    (out/"summary.json").write_text(json.dumps(dict(passed=True,scope="source and independent byte oracle; TLC recorded separately",**meta),indent=2)+"\n")
    print(f"OOB pipeline: {len(traces)} source traces and independent full-buffer comparisons passed; logs: {out}",flush=True)
    if args.source_only: return 0
    tests=[case(f"source-{i//4:03d}",traces[i:i+4]) for i in range(0,len(traces),4)]
    tests+=controls(traces)
    options=["--timeout",str(args.timeout),"--case",args.case]
    if args.download: options.append("--download")
    return tlc.main(options,cases=tests,modules={"NandOobPipeline":MODEL},metadata=meta)


if __name__=="__main__":
    raise SystemExit(main())
