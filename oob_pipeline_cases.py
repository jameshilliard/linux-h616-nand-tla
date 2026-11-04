# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent decoded/raw inputs for the randomized OOB pipeline checker."""
import copy
import hashlib
import json
from pathlib import Path
import re
import struct

from codec_oracle import scramble

HERE = Path(__file__).resolve().parent
MODEL = dict(invariants=["BatchesMatch", "CaptureMatches", "ClassificationMatches",
                         "TransportMatches", "ProtectedPreserved", "OutputMatches",
                         "AccountingMatches", "ReturnMatches"],
             actions=["Batch", "Capture", "Classify", "Read", "Convert", "Account", "Finish"])


def advance(state, clocks):
    for _ in range(clocks):
        state = (state >> 1) | (((state ^ (state >> 1)) & 1) << 14)
    return state


def inputs():
    manifest = json.loads((HERE / "reference/compat/source.json").read_text())
    raw = (HERE / "reference/compat/ndfc_base.c").read_bytes()
    assert hashlib.sha256(raw).hexdigest() == manifest["files"]["ndfc_base.c"]
    table = re.search(r"const u32 random_seed\[128\] = \{(.*?)\};", raw.decode(), re.S)[1]
    seeds = [int(x,16) for x in re.findall(r"0x[0-9a-f]+",table)]
    assert len(seeds) == 128
    traces = []
    layouts = [(False,512,1),(False,1024,2),(False,1024,4),(False,1024,16),
               (True,1024,2),(True,1024,4),(True,1024,32)]
    pages = (0,1,63,64,127,128,255)
    blocks = (32,64,128,256)
    prefixes = [[],[0],*[ [i] for i in range(1,10) ],[8,9],[4,5,6,7,8,9],list(range(10))]

    def add(layout,dma,want,scenario,prefix,serial,fail=0):
        variable,step,n=layout
        user = [min(n*4,16)]+[0]*(n-1) if variable else [4]*n
        page,ppb=pages[serial%len(pages)],blocks[(serial//len(pages))%len(blocks)]
        parity,tail=56,(0,1,13)[serial%3]
        spare=sum(user)+n*parity+tail
        ecc_state=advance(seeds[page%min(ppb,128)],step*8+15)
        ecc_mask=[]
        state=ecc_state
        for _ in range(max(user)):
            ecc_mask.append(state&255)
            state=advance(state,8)
        kinds = [scenario]*n if scenario<6 else (
            [i%6 for i in range(n)] if scenario==6 else
            [3]+[0]*(n-1) if scenario==7 else [0]*(n-1)+[3])
        physical_prefix=[255]*10
        for i in prefixes[prefix]: physical_prefix[i]=0 if i%2 else 0x5a
        decoded=[165]*spare
        physical=[(i*37+serial*13+7)&255 for i in range(spare)]
        parity_mask=[0]*spare
        off,pos=0,0
        for length in user:
            for j in range(length):
                plain=physical_prefix[pos] if pos<10 else (pos*17+serial)&255
                decoded[off+j]=plain^ecc_mask[j]
                # Distinct raw metadata deliberately cannot supply the
                # ECC-corrected prefix. This models corrected physical errors,
                # not a new BCH decoder or plausible errors for every scenario.
                physical[off+j]=plain^1
                pos+=1
            mask=scramble(bytes(length+parity),ecc_state)[length:]
            parity_mask[off+length:off+length+parity]=mask
            off+=length+parity
        tail_mask=list(scramble(bytes(tail),seeds[page%min(ppb,128)]))
        trace=dict(id=len(traces),variable=variable,step=step,parity=parity,
                   page=page,ppb=ppb,dma=dma,want=want,fail=fail,
                   kinds=kinds,user=user,decoded=decoded,raw=physical,
                   eccMask=ecc_mask,parityMask=parity_mask,tailMask=tail_mask)
        traces.append(trace)

    # Cross the six independent hardware status encodings, mixed batches,
    # first/last errors, both representations and data-only/OOB reads.
    for li,layout in enumerate(layouts):
        for dma in (False,True):
            for want in (False,True):
                for scenario in range(9):
                    serial=li*36+int(dma)*18+int(want)*9+scenario
                    prefix=0 if scenario<2 else serial%len(prefixes)
                    add(layout,dma,want,scenario,prefix,serial)
        # Exercise every marker/prefix position, padding, and the special
        # first/127th-page legacy rule with exceptions present.
        for prefix in range(len(prefixes)):
            add(layout,False,False,3,prefix,li*28+prefix)
        # Fail every actual raw OOB call, plus success and an unreachable
        # failure index, while keeping corrected and failed counters nonzero.
        for dma in (False,True):
            calls=1 if layout[0] else layout[2]
            for fail in range(calls+2):
                add(layout,dma,True,8,13,li*11+3,fail)
    return traces


def pack_vectors(traces):
    data=bytearray()
    for t in traces:
        data+=struct.pack("<12I",0x4f4f4254,t["id"],t["variable"],len(t["user"]),
                          t["step"],t["parity"],len(t["raw"]),t["page"],t["ppb"],
                          t["dma"],t["want"],t["fail"])
        data+=bytes(t["kinds"])+bytes(t["decoded"])+bytes(t["raw"])
    return bytes(data)


def freeze(value):
    if isinstance(value,list): return tuple(freeze(v) for v in value)
    if isinstance(value,dict): return {k:freeze(v) for k,v in value.items()}
    return value


def case(name,traces,expected=None):
    return dict(name="oob-pipeline-"+name,module="NandOobPipeline",
                kind="mutation" if expected else "positive",
                expected=expected,constants=dict(Traces=freeze(traces)))


def controls(traces):
    good=next(t for t in traces if not t["dma"] and t["want"] and
              t["observed"]["reads"] and t["observed"]["ret"]>=0 and t["tailMask"])
    dma=next(t for t in traces if t["dma"] and t["want"] and
             t["observed"]["reads"] and t["observed"]["ret"]>=0)
    error=next(t for t in traces if t["observed"]["ret"]==-110)
    zero=next(t for t in traces if t["observed"]["main"][1])
    tests=[]
    def change(name,t,invariant,edit):
        t=copy.deepcopy(t)
        edit(t)
        tests.append(case(name,[t],invariant))
    change("corrupt-capture",good,"CaptureMatches",lambda t:t["observed"]["captured"].__setitem__(0,t["observed"]["captured"][0]^1))
    change("wrong-batch-status",good,"BatchesMatch",lambda t:t["observed"]["batches"][0]["errors"].__setitem__(0,t["observed"]["batches"][0]["errors"][0]^1))
    change("wrong-erased-decision",good,"ClassificationMatches",lambda t:t["observed"].update(erased=not t["observed"]["erased"]))
    change("clobbered-protected",good,"OutputMatches",lambda t:t["observed"]["oob"].__setitem__(0,t["observed"]["oob"][0]^1))
    change("wrong-tail-phase",good,"OutputMatches",lambda t:t["tailMask"].__setitem__(0,t["tailMask"][0]^1))
    change("wrong-parity-phase",good,"OutputMatches",lambda t:t["parityMask"].__setitem__(t["user"][0],t["parityMask"][t["user"][0]]^1))
    change("changed-dma-parity",dma,"OutputMatches",lambda t:t["observed"]["oob"].__setitem__(t["user"][0],t["observed"]["oob"][t["user"][0]]^1))
    change("stats-on-error",error,"AccountingMatches",lambda t:t["observed"]["stats"].__setitem__(0,8))
    change("masked-transport-error",error,"ReturnMatches",lambda t:t["observed"].update(ret=0))
    change("read-through-protected",good,"TransportMatches",lambda t:t["observed"]["reads"][0].update(off=0,dest=0))
    change("zero-loses-precedence",zero,"OutputMatches",lambda t:t["observed"].update(main=[False,False,True]))
    return tests
