#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Cross-check BCH/page encoding with independent polynomial and image builders."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import random
import subprocess
import struct
import sys
import tempfile

from codec_oracle import encode, generator, scramble

HERE = Path(__file__).resolve().parent
COMPAT = r'''
#ifndef HOST_BCH_COMPAT
#define HOST_BCH_COMPAT
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <endian.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define GFP_KERNEL 0
#define kmalloc(n,f) malloc(n)
#define kzalloc_obj(x) calloc(1,sizeof(x))
#define kfree free
#define cpu_to_be32 htobe32
#define WARN_ON(x) (!!(x))
#define swap(a,b) do { __typeof__(a) tmp=(a); (a)=(b); (b)=tmp; } while (0)
#define EXPORT_SYMBOL_GPL(x)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define printk(...) fprintf(stderr,__VA_ARGS__)
#define KERN_ERR ""
static int fls(unsigned v) { return v ? 32-__builtin_clz(v) : 0; }
static u8 bitrev8(u8 v) { u8 r=0; for (int i=0;i<8;i++) { r=(r<<1)|(v&1); v>>=1; } return r; }
#endif
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--candidate-source", type=Path,
                        help="compose BCH vectors with this sunxi_nand.c instead of the pinned driver")
    args = parser.parse_args()
    pin = json.loads((HERE / "reference/codec/source.json").read_text())
    results = HERE / "results"
    results.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="codec-", dir=results))
    for name, digest in pin["files"].items():
        data = (HERE / "reference/codec" / name).read_bytes()
        assert hashlib.sha256(data).hexdigest() == digest, name
        (out / name).write_bytes(data)
    (out / "compat/linux").mkdir(parents=True)
    (out / "compat/asm").mkdir()
    for name in ("kernel.h", "init.h", "module.h", "slab.h", "bitops.h", "bitrev.h", "types.h"):
        (out / "compat/linux" / name).write_text('#include "../host.h"\n')
    # Let libc's bits/errno.h include the real system linux/errno.h.
    (out / "compat/host.h").write_text(COMPAT)
    (out / "compat/asm/byteorder.h").write_text('#include "../host.h"\n')
    (out / "compat/linux/bch.h").write_bytes((out / "bch.h").read_bytes())
    (out / "common.h").write_text('#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))\n#define VERSION "pinned-validation"\n')
    (out / "portable_endian.h").write_text('#include <endian.h>\n')
    for name in ("codec.c", "builder.c"):
        (out / name).write_bytes((HERE / "tests" / name).read_bytes())
    flags = [args.cc, "-std=gnu11", "-O1", "-g", "-Icompat", "-fno-strict-aliasing",
             "-fno-strict-overflow", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie"]
    strengths = (16,24,28,32,40,44,48,52,56,60,64,68,72,76,80)
    builds = {}

    def build(t):
        # The upstream library supports >64 via its existing constant-parameter
        # configuration. Do not edit BCH_MAX_T or any library function body.
        key = t if t > 64 else 0
        if key not in builds:
            binary = "codec-" + str(key)
            extra = [] if not key else ["-DCONFIG_BCH_CONST_PARAMS", "-DCONFIG_BCH_CONST_M=14", f"-DCONFIG_BCH_CONST_T={t}"]
            run = subprocess.run(flags + extra + ["codec.c", "-o", binary], cwd=out, capture_output=True)
            (out / (binary + "-build.log")).write_bytes(run.stdout + run.stderr)
            assert not run.returncode, run.stderr.decode()
            builds[key] = binary
        return builds[key]

    run = subprocess.run(flags + ["builder.c", "-o", "builder"], cwd=out, capture_output=True)
    (out / "builder-build.log").write_bytes(run.stdout + run.stderr)
    assert not run.returncode, run.stderr.decode()
    count = 0
    vectors = []
    beyond = {"rejected": 0, "miscorrected": 0, "recovered": 0}

    def execute(binary, argv, data):
        p = subprocess.run([str(out / binary), *map(str, argv)], input=data, capture_output=True, timeout=30)
        assert not p.returncode and not p.stderr, (binary, argv, p.returncode, p.stderr.decode())
        return p.stdout

    for t in strengths:
        binary = build(t)
        for step in (512,1024):
            for user in (0,4,16,32):
                data = bytes((i*37+(i>>8)*13+t) & 255 for i in range(step+user))
                parity = encode(data,t)
                assert execute(binary,("encode",t,len(data)),data) == parity
                for errors in (0,1,t//2,t):
                    # Mix data, protected OOB, and parity locations without repeats.
                    damaged=bytearray(data+parity)
                    # The byte container can have padding beyond deg(g), notably
                    # t=80. Such bits are not BCH symbols and are not correctable.
                    spots=list(range(len(data)*8+generator(t)[1]))
                    random.Random(t*100000+step*10+user+errors).shuffle(spots)
                    for bit in spots[:errors]: damaged[bit//8] ^= 1 << (bit%8)
                    result=execute(binary,("decode",t,len(data)),damaged)
                    status, corrected = result.split(b"\n",1)
                    assert int(status)==errors and corrected==data, (t,step,user,errors,status)
                    count+=1
                # Over-capacity is deliberately a selected witness, not a claim
                # that BCH detects every >t corruption (miscorrection is possible).
                damaged=bytearray(data+parity)
                random.Random(900000+t*100000+step*10+user).shuffle(spots)
                for bit in spots[:2*t+1]: damaged[bit//8] ^= 1 << (bit%8)
                assert sum((a^b).bit_count() for a,b in zip(damaged,data+parity)) == 2*t+1
                result=execute(binary,("decode",t,len(data)),damaged)
                status, corrected=result.split(b"\n",1)
                outcome = "rejected" if int(status)<0 else "recovered" if corrected==data else "miscorrected"
                beyond[outcome] += 1
                count+=1
                vectors.append(dict(t=t,step=step,user=user,parity_sha256=hashlib.sha256(parity).hexdigest()))
    # Cross-version image oracle: actual sunxi-tools page encoder, independent
    # Python BCH/randomizer and layout reconstruction. Includes seed wrap.
    src=(out / "nand-image-builder.c").read_text()
    seed_text=re.search(r"default_scrambler_seeds\[\] = \{(.*?)\};",src,re.S)[1]
    seeds=[int(x,16) for x in re.findall(r"0x[0-9a-f]+",seed_text)]
    images=0
    for t in (16,32,64,80):
        for step in (512,1024):
            if (2048//step)*(4+(14*t+7)//8)>512: continue
            for randomized in (0,1):
                for page in (0,1,63,64,127,128):
                    data=bytes((i*19+page+1)&255 for i in range(2048))
                    physical=execute("builder",(t,step,randomized,page),data)
                    expected=bytearray(b"\xff"*2560)
                    off=2048
                    for i in range(2048//step):
                        codeword=data[i*step:(i+1)*step]+b"\xff"*4
                        codeword+=encode(codeword,t)
                        if randomized: codeword=scramble(codeword,seeds[page%64])
                        expected[i*step:(i+1)*step]=codeword[:step]
                        expected[off:off+len(codeword)-step]=codeword[step:]
                        off+=len(codeword)-step
                    if randomized: expected[off:]=scramble(expected[off:],seeds[page%64])
                    expected[2048:2050]=b"\xff\xff"
                    assert physical==expected,(t,step,randomized,page)
                    images+=1
    assert beyond["rejected"] > 0
    # Compose actual BCH correction with the exact driver packing/BBM helpers.
    # Four even-byte parity sizes avoid claiming the hardware's unused padding
    # byte value for odd BCH byte counts. The codec-only matrix covers those.
    bridge=bytearray()
    bridge_cases=0
    bridge_round_trips=0
    wrong_seed=0
    for t in (16,32,64,80):
        for step,variable in ((512,0),(1024,0),(1024,1)):
            if not variable and t>64:
                continue  # Legacy controller strength table ends at 64.
            for fmt in (0,1,2):
                randomize,vendor=fmt!=0,fmt==2
                for page in (0,63,64,128):
                    steps=1 if step==512 else 4  # Core upgrades larger pages to 1-KiB steps.
                    lengths=([min(steps*4,16)]+[0]*(steps-1) if vendor else [32]*steps) if variable else [4]*steps
                    parity=(14*t+7)//8
                    spare=sum(lengths)+steps*parity+13
                    original=bytes((i*29+page*7+t)&255 for i in range(steps*step))
                    logical=bytes((i*13+page*11+3)&255 for i in range(spare))
                    physical=bytearray(b"\xff"*(len(original)+spare))
                    corrected=bytearray()
                    off=0
                    for k,length in enumerate(lengths):
                        main=original[k*step:(k+1)*step]
                        user=logical[off:off+length]
                        if randomize and not vendor and k==0:
                            mask=scramble(bytes(step+length),seeds[page%64])[step:]
                            user=bytes(b^(mask[j] if j<2 else 0) for j,b in enumerate(user))
                        message=main+user
                        word=message+encode(message,t)
                        stored=scramble(word,seeds[page%64]) if randomize else word
                        physical[k*step:(k+1)*step]=stored[:step]
                        physical[len(original)+off:len(original)+off+length+parity]=stored[step:]
                        # Corrupt physical bits across data/OOB/parity, then
                        # de-randomize and correct with Linux's actual decoder.
                        damaged=bytearray(stored)
                        positions=list(range(len(message)*8+generator(t)[1]))
                        random.Random(t*999+page*17+k).shuffle(positions)
                        for bit in positions[:t]: damaged[bit//8]^=1<<(bit%8)
                        received=scramble(damaged,seeds[page%64]) if randomize else damaged
                        status,data=execute(build(t),("decode",t,len(message)),received).split(b"\n",1)
                        assert int(status)==t and data==message
                        corrected+=data
                        if randomize and k==0:
                            wrong=scramble(stored,seeds[(page%64+1)%64])
                            status,data=execute(build(t),("decode",t,len(message)),wrong).split(b"\n",1)
                            assert int(status)<0 or data!=message
                            wrong_seed+=1
                        off+=length+parity
                    tail=logical[off:]
                    physical[len(original)+off:]=scramble(tail,seeds[page%64]) if randomize else tail
                    bridge+=struct.pack("<9I",0x4e414e44,step,steps,parity,variable,vendor,randomize,page,spare)
                    bridge+=original+logical+physical+corrected
                    bridge_cases+=1
                    bridge_round_trips+=1 if step==512 else 2
    vector_path=out / "driver-vectors.bin"
    vector_path.write_bytes(bridge)
    # Vector headers have a fixed little-endian serialization.
    assert sys.byteorder=="little", "bridge runner currently requires a little-endian host"
    env=dict(os.environ,NAND_CODEC_VECTORS=str(vector_path))
    command=[sys.executable,str(HERE/"run_c.py"),"--cc",args.cc]
    driver = args.candidate_source or HERE / "reference/sunxi_nand.c"
    driver_data = driver.read_bytes()
    driver_hash = hashlib.sha256(driver_data).hexdigest()
    if args.candidate_source:
        # Match the digest to the bytes tested even if the kernel checkout
        # changes while this subprocess is compiling.
        snapshot = out / "candidate-sunxi_nand.c"
        snapshot.write_bytes(driver_data)
        command += ["--candidate-source", str(snapshot)]
    checked=subprocess.run(command,
                           env=env,text=True,capture_output=True,timeout=180)
    (out / "driver-bridge.log").write_text(checked.stdout+checked.stderr)
    assert not checked.returncode and "real BCH/driver composition:" in checked.stdout, checked.stdout+checked.stderr
    print(checked.stdout)
    summary=dict(passed=True,codec_cases=count,page_images=images,vectors=vectors,beyond_capacity=beyond,
                 driver_vectors=bridge_cases,driver_round_trips=bridge_round_trips,wrong_seed_witnesses=wrong_seed,
                 source=pin,driver_sha256=driver_hash,candidate=bool(args.candidate_source),
                 limits=["No captured hardware/BSP golden image", "Beyond-capacity BCH errors can miscorrect",
                    "sunxi-tools image builder describes the legacy four-user-byte layout, not H616 vendor packing"])
    (out / "summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(f"BCH: {count} correction/over-capacity cases; {len(vectors)} independent parity vectors; {images} sunxi-tools page images")
    print("ALL CODEC CHECKS PASSED")
    print("Logs:",out)


if __name__ == "__main__":
    main()
