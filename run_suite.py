#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the standard models and source matrices against one candidate snapshot.

This does not reinterpret pinned known-bug counterexamples as candidate fixes.
Dedicated trace runners are included separately from the standard TLC matrix.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from boundary_common import HERE
from cases import CASES, MODULES
import run as tlc


def inputs():
    """Record executable inputs; do not silently mix edits during a run."""
    paths=set(HERE.glob("*.py")) | {HERE/"source.json"}
    for directory in ("tests","models","reference"):
        paths.update(p for p in (HERE/directory).rglob("*") if p.is_file())
    paths.update(p for p in (HERE/"hardware").glob("*")
                 if p.suffix in (".py", ".json", ".c", ".h"))
    return {str(p.relative_to(HERE)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-source",type=Path)
    parser.add_argument("--workers",type=int,default=4)
    parser.add_argument("--module",choices=sorted(MODULES))
    parser.add_argument("--source-only",action="store_true",
                        help="run the fourteen ordinary C matrices without TLC")
    args=parser.parse_args()
    if args.module:
        return tlc.main([],cases=[c for c in CASES if c["module"]==args.module],
                        modules={args.module:MODULES[args.module]})
    if not args.candidate_source: parser.error("--candidate-source is required")
    if args.workers<1: parser.error("--workers must be positive")
    (HERE/"results").mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix="suite-",dir=HERE/"results"))
    input_hashes=inputs()
    source=args.candidate_source.read_bytes()
    candidate=out/"candidate-sunxi_nand.c"; candidate.write_bytes(source)
    shared=["--candidate-source",str(candidate)]
    jobs=[("hardware-capture","run_hardware.py",[]),
          ("c","run_c.py",shared),
          ("lowlevel","run_lowlevel.py",shared+["--expect-count-fixed"]),
          ("lifecycle","run_lifecycle.py",shared+["--expect-available-children","--expect-deferred-publication"]),
          ("codec","run_codec.py",shared),
          ("composed","run_composed.py",shared),
          ("transport","run_transport.py",shared),
          ("transport-count","run_transport.py",shared+["--count-boundary","--expect-count-fixed"]),
          ("discovery","run_discovery_boundary.py",shared),
          ("lifetime","run_lifetime_boundary.py",shared),
          ("core","run_core.py",[]),
          ("providers","run_providers.py",[]),
          ("provider-boundary","run_provider_boundary.py",[]),
          ("historical","run_historical.py",[]),
          ("mtd","run_mtd_boundary.py",shared)]
    followups={}
    if not args.source_only:
        for name,script,options in jobs:
            if name=="lowlevel":
                options.append("--recovery-traces")
                followups[name]="run_recovery_refinement.py"
            elif name=="lifecycle":
                options.append("--probe-traces")
                followups[name]="run_probe_refinement.py"
        jobs += [("oob-pipeline","run_oob_pipeline.py",shared),
                 ("request-trace","run_request_refinement.py",shared),
                 ("fd-trace","run_fd_lifetime.py",shared),
                 ("request-ordering","run_ordering.py",[]),
                 ("fd-locking","run_fd_locking.py",[])]
        jobs += [("tlc-"+m,"run_suite.py",["--module",m]) for m in MODULES]
    def execute(job):
        name,script,options=job
        command=[sys.executable,str(HERE/script),*options]
        try:
            result=subprocess.run(command,cwd=HERE,text=True,capture_output=True,timeout=7200)
            log=result.stdout+result.stderr; rc=result.returncode
            if not rc and name in followups:
                directories=re.findall(r"^Logs: (.+)$",result.stdout,re.M)
                if len(directories)!=1:
                    raise RuntimeError("missing or ambiguous source result directory")
                post=subprocess.run([sys.executable,str(HERE/followups[name]),directories[0]],
                                    cwd=HERE,text=True,capture_output=True,timeout=7200)
                log+=post.stdout+post.stderr; rc=post.returncode
        except (subprocess.TimeoutExpired, RuntimeError) as error:
            log=locals().get("log","")+f"\nSUITE JOB FAILED: {error}\n"; rc=-1
        (out/(name+".log")).write_text(log)
        print(("PASS " if not rc else "FAIL ")+name,flush=True)
        # Keep result references relative to this checkout for portable records.
        directories=re.findall(r"(?:^Logs: |logs: )([^\n]+)$",log,re.M)
        reports=[]
        for value in directories:
            path=Path(value)/"summary.json"
            if path.is_file() and path.is_relative_to(HERE):
                reports.append(str(path.relative_to(HERE)))
        return dict(name=name,returncode=rc,passed=rc==0,reports=sorted(set(reports)))
    print("Suite logs:",out,flush=True)
    records=[]
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for future in as_completed([pool.submit(execute,job) for job in jobs]):
            records.append(future.result())
            complete=len(records)==len(jobs)
            unchanged=inputs()==input_hashes if complete else None
            (out/"summary.json").write_text(json.dumps(dict(
                complete=complete,passed=complete and unchanged and all(r["passed"] for r in records),
                source_sha256=hashlib.sha256(source).hexdigest(),
                executable_inputs_sha256=input_hashes,inputs_unchanged=unchanged,
                jobs=sorted(records,key=lambda r:r["name"])),indent=2)+"\n")
    print(f"{sum(r['passed'] for r in records)}/{len(records)} jobs passed; logs: {out}")
    if not unchanged: print("FAIL executable inputs changed during the suite")
    return 0 if unchanged and all(r["passed"] for r in records) else 1


if __name__=="__main__":
    raise SystemExit(main())
