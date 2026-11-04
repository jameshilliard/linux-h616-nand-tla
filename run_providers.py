#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute pinned external-DMA config/stop entrypoints; expose their boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from run_core import extract
from source_coverage import build_and_collect

HERE=Path(__file__).resolve().parent
FUNCTIONS={"sun4i-dma.c":["sun4i_dma_terminate_all","sun4i_dma_config"],
           "sun6i-dma.c":["sun6i_dma_terminate_all","sun6i_dma_config"]}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc",default="gcc")
    args=parser.parse_args()
    pin=json.loads((HERE/"reference/providers/source.json").read_text())
    sources={}; bodies=[]; hashes={}
    for name,names in FUNCTIONS.items():
        src=(HERE/"reference/providers"/name).read_text()
        assert hashlib.sha256(src.encode()).hexdigest()==pin["files"][name]
        sources[name]=src
        for define in re.findall(r"^#define (?:SUN4I_[DN]DMA_CFG_REG|DMA_CHAN_ENABLE\w*|DMA_CHAN_PAUSE\w*)\s+[^\n]+",src,re.M):
            bodies.append(define)
        for func in names:
            body=extract(src,func); line=src[:src.index(body)].count("\n")+1
            bodies.append(f'#line {line} "{name}"\n'+body)
            hashes[func]=hashlib.sha256(body.encode()).hexdigest()
    (HERE/"results").mkdir(exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix="providers-",dir=HERE/"results"))
    combined=(HERE/"tests/providers.h").read_text()+"\n"+"\n".join(bodies)+\
        '\n#line 1 "provider-tests.c"\n'+(HERE/"tests/providers.c").read_text()
    (out/"check.c").write_text(combined)
    command=[args.cc,"-std=gnu11","-O1","-g","-Wall","-Wextra","-Werror",
        "-Wno-unused-parameter","-fno-strict-aliasing","-fno-strict-overflow",
        "-fsanitize=address,undefined","-fno-sanitize-recover=all","-no-pie","check.c","-o","check"]
    build=subprocess.run(command,cwd=out,text=True,capture_output=True)
    (out/"build.log").write_text(build.stdout+build.stderr)
    assert not build.returncode,build.stdout+build.stderr
    run=subprocess.run(["./check"],cwd=out,text=True,capture_output=True,timeout=30)
    (out/"check.log").write_text(run.stdout+run.stderr)
    assert not run.returncode and not run.stderr,run.stdout+run.stderr
    coverage=build_and_collect(out,command,sources,FUNCTIONS)
    assert all(v["calls"] for v in coverage["functions"].values())
    (out/"summary.json").write_text(json.dumps(dict(passed=True,source=pin,functions=hashes,
        output=run.stdout,coverage=coverage,limits=["No real provider IRQ/tasklet concurrency",
        "MMIO stop is a request; bus retirement is an independent hardware contract"]),indent=2)+"\n")
    print(run.stdout)
    print("Logs:",out)


if __name__=="__main__":
    main()
