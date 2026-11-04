#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose captured H616 NORMAL semantics with actual page/core callers.

128-byte OOB matches the tested part. Larger/odd OOB geometries are synthetic
attach-admitted cases, not claims that such NAND parts are present on a board.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from boundary_common import HERE, check
from codec_oracle import scramble
from driver_boundary import fixture_body
from oob_pipeline_cases import advance
from run_request_refinement import generate
from run_transport import replace_once


def units(candidate, spare):
    unit, hashes, src = generate(candidate)
    driver = unit['driver.c']
    # Only hardware fixtures change. Extracted kernel function hashes stay
    # identical to the ordinary whole-request runner.
    old = fixture_body(driver, 'advance_seed')
    driver = replace_once(driver, old, old[:old.index('{')] + '{ (void)len; }')
    prelude = ('static bool measured_dirty, measured_inject_dirty;\n'
               'static unsigned measured_write_fault, measured_confirms;\n'
               'static void measured_normal(u8 *,unsigned,bool);\n')
    driver = replace_once(driver, 'static u16 transport_seed(void)', prelude + 'static u16 transport_seed(void)')
    driver = replace_once(driver, 'transform(staging+column,limit,transport_seed()); advance_seed(limit);',
                          'measured_normal(staging+column,limit,true);')
    driver = replace_once(driver, 'transform(sram,limit,transport_seed()); advance_seed(limit);',
                          'measured_normal(sram,limit,false);')
    driver = replace_once(driver, 'request_event("confirm",0,0);',
                          'measured_confirms++; request_event("confirm",0,0);')
    driver = replace_once(driver, 'bool fail=wire_fail && wire_count==wire_fail;',
        'bool confirm_fault=measured_write_fault==3 && (cmd&BIT(22)) && (cmd&255)==0x10;\n'
        '    if(confirm_fault) wire_accepted=1;\n'
        '    bool fail=confirm_fault || (wire_fail && wire_count==wire_fail);')
    driver = replace_once(driver, 'bool write=cmd&BIT(20);',
        'bool write=cmd&BIT(20);\n'
        '    if(write && (cmd&BIT(21)) && measured_write_fault && measured_write_fault<3 &&\n'
        '       column>=B_PAGE+sunxi_get_oob_offset(&persistent_chip,&persistent_chip.ecc,2)) {\n'
        '        fail=true; wire_accepted=measured_write_fault==2 ? 4 : 0;\n'
        '        request_wire_error(); if(!wire_accepted) return;\n'
        '    }')
    extra = (HERE/'tests/measured_request_driver.c').read_text()
    # Tentative declarations in the prelude are needed by fixture dispatch.
    driver += '\n#line 1 "measured_request_driver.c"\n' + extra
    unit['driver.c'] = replace_once(driver, 'B_SPARE=256', f'B_SPARE={spare}')
    transport = unit['transport.c']
    transport = 'extern void measured_soft_reset(_Bool);\n' + transport
    transport = replace_once(transport, 'hw.active=hw.outstanding_bus=false;\n        value=0;',
                              'measured_soft_reset(hw.active || hw.outstanding_bus);\n'
                              '        hw.active=hw.outstanding_bus=false;\n        value=0;')
    unit['transport.c'] = replace_once(transport, '.oobsize=256', f'.oobsize={spare}')
    unit['check.c'] = replace_once(unit['check.c'], (HERE/'tests/request_core.c').read_text(),
                                   (HERE/'tests/measured_request_core.c').read_text())
    unit['check.c'] = replace_once(unit['check.c'], 'SPARE=256', f'SPARE={spare}')
    return unit, hashes, src


def vendor_seed(page):
    data = (HERE/'reference/compat/ndfc_base.c').read_bytes()
    manifest = json.loads((HERE/'reference/compat/source.json').read_text())
    assert hashlib.sha256(data).hexdigest() == manifest['files']['ndfc_base.c']
    table = re.search(r'const u32 random_seed\[128\] = \{(.*?)\};', data.decode(), re.S)[1]
    return [int(v, 16) for v in re.findall(r'0x[0-9a-f]+', table)][page % 128]


def verify(out, spare):
    rows = [json.loads(s) for s in (out/'measured-pages.jsonl').read_text().splitlines()]
    observed = (out/'measured-pages.bin').read_bytes()
    pos = 0
    seed = vendor_seed(17)
    for row in rows:
        if row['kind'] != 'roundtrip':
            continue
        fmt = row['format']
        lengths = [8, 0] if fmt == 2 else ([16, 0] if spare == 128 else [32, 32])
        assert row['user'] == lengths
        tail = 112 + sum(lengths)
        assert row['tail'] == tail and row['free'] == sum(lengths)-2
        data = bytes((i*19+(i>>8)*7+fmt)&255 for i in range(2048))
        main = b''.join(scramble(data[i:i+1024], seed) if fmt else data[i:i+1024]
                        for i in range(0, 2048, 1024))
        oob = bytearray()
        for length in lengths:
            user = bytes([255]*length)
            if fmt:
                user = scramble(user, advance(seed, 8192))
            if fmt == 1 and not oob:
                user = bytes([255, 255]) + user[2:]
            oob += user + bytes([0x5b]*56)
        for offset in range(0, spare-tail, 1024):
            chunk = bytes([255]*min(1024, spare-tail-offset))
            oob += scramble(chunk, seed) if fmt else chunk
        expected = main + oob
        assert observed[pos:pos+2048+spare] == expected, ('stored image', row)
        pos += 2048+spare
        assert observed[pos:pos+2048] == data, ('main read', row)
        pos += 2048
        metadata = bytes([255]*(sum(lengths)-2))
        assert observed[pos:pos+len(metadata)] == metadata, ('AUTO metadata', row)
        pos += len(metadata)
        extra = bytearray([255]*(spare-tail))
        if fmt == 2:
            # The vendor reader fetches raw bytes, then descrambles the
            # entire tail in software as ONE continuous stream. This is a
            # named failure witness when the writer issued >1 command.
            extra = bytearray(scramble(oob[tail:], seed))
        elif fmt and (spare-tail) % 4:
            # Last tail byte survives in its on-media randomized form.
            extra[-1] = oob[-1]
        assert observed[pos:pos+len(extra)] == extra, ('PLACE tail', row)
        pos += len(extra)
        # Deliberately wrong continuous-phase oracle must distinguish an
        # admitted multicommand tail; this is not merely a round-trip check.
        if fmt and spare-tail > 1024:
            assert oob[tail:] != scramble(bytes([255]*(spare-tail)), seed)
        if fmt == 1 and (spare-tail) % 4:
            assert extra != bytes([255]*len(extra))
        if fmt == 2 and spare-tail > 1024:
            assert extra[:1024] == bytes([255]*1024)
            assert extra[1024:] != bytes([255]*(len(extra)-1024))
    assert pos == len(observed) and len(rows) == 30
    traces = [json.loads(s) for s in (out/'request-traces.jsonl').read_text().splitlines()]
    assert len(traces) == 64 and [t['id'] for t in traces] == list(range(64))
    for trace in traces:
        assert not trace['events'][-1][3] and not trace['events'][-1][4]
    return dict(spare=spare, stored_images=12, retry_outcomes=12, write_errors=6,
                source_calls=64, expected_vendor_tail_witnesses=4 if spare>1144 else 0,
                expected_normal_nonword_witnesses=4 if spare%4 else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate-source', type=Path)
    parser.add_argument('--spare', type=int, choices=(128, 256, 2048, 2049))
    args = parser.parse_args()
    geometries = (128, 256, 2048, 2049) if args.candidate_source else (128, 256)
    for spare in (args.spare,) if args.spare else geometries:
        unit, hashes, src = units(args.candidate_source, spare)
        names = {}
        for key in hashes:
            filename, name = key.split(':', 1)
            names.setdefault(filename, []).append(name)
        out = check('measured-request', unit, hashes, coverage=(src, names),
                    require_all_called=False,
                    artifacts=('measured-pages.bin', 'measured-pages.jsonl', 'request-traces.jsonl'),
                    limits=['H616 block-mode NORMAL semantics from finite capture; no new board run',
                            'Actual core/page/DMA/command bodies; modeled protected-ECC/parity',
                            'Soft-reset rotation is an injected observed outcome, not a causal RTL model',
                            'Large/odd OOB cases are synthetic; no physical-part reachability claim'])
        report = json.loads((out/'summary.json').read_text())
        report['hardware_inputs_sha256'] = {
            name: hashlib.sha256((HERE/'hardware'/name).read_bytes()).hexdigest()
            for name in ('h616-capture-20261104.json', 'h616-reset-20261104.json')}
        try:
            report['measured_oracle'] = verify(out, spare)
        except Exception:
            report['passed'] = False
            raise
        finally:
            (out/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
        print('Independent measured oracle:', report['measured_oracle'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
