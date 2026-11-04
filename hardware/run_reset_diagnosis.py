#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Identify first-read corruption using distinct, independently encoded pages.

Destructive only to the previously authorized block 7. Requires RAM-only boot,
no UBI attachment, fresh independent backups, and the matching native bridge.
All published byte strings are synthetic; raw backups stay outside this repo.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from hardware.run_probe import Probe, RAW, SEEDS, request, verified_backup, restore, page_image
from hardware.run_extended import native, checked_native, NREAD, NWRITE


def pattern(page):
    return hashlib.shake_256(f'nfc-reset-cause-page-{page}'.encode()).digest(RAW)


def read_request(kind, page):
    if kind == 'raw':
        return request(1, page=page)
    if kind == 'mtd':
        return request(22, page=page, length=2048, chunk=0)
    return native(NREAD, page=page, mode={'pio': 0, 'dma': 1}[kind], flags=0)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--repeat', type=int, default=3)
    p.add_argument('--followup-matrix', action='store_true',
                   help='observe/clear SRAM, reset NAND, or read one raw chunk before the native read')
    p.add_argument('--bus-reset-matrix', action='store_true',
                   help='compare completed, aborted, and aborted-plus-bus-reset transfers')
    p.add_argument('--ram-only-confirmed', action='store_true', required=True)
    p.add_argument('command', nargs=argparse.REMAINDER)
    a = p.parse_args()
    directory = a.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]):
        p.error('private evidence must remain outside the public repository')
    if not a.command or not 1 <= a.repeat <= 100:
        p.error('transport command and bounded positive repeat count required')
    if a.followup_matrix and a.bus_reset_matrix:
        p.error('select only one matrix')
    path = directory / ('reset-followup.json' if a.followup_matrix else
                        'reset-bus.json' if a.bus_reset_matrix else 'reset-diagnosis.json')
    if path.exists():
        p.error('refusing to overwrite evidence')
    backup = verified_backup(directory)
    probe = Probe(a.command)
    actual = b''.join(r[2] for r in probe.run([request(1, page=page) for page in range(448, 512)]))
    assert actual == backup[448 * RAW:]
    report = dict(schema=1, pattern='SHAKE256(nfc-reset-cause-page-N)',
                  followup_matrix=a.followup_matrix, bus_reset_matrix=a.bus_reset_matrix,
                  rows=[], restored=False)
    logical = {page: pattern(page) for page in (448, 449, 450)}
    physical = {page: page_image(data, page) for page, data in logical.items()}
    try:
        probe.run([request(4)])
        for page, data in logical.items():
            ret, regs, _ = checked_native(probe, native(NWRITE, page=page, data=data, flags=0))
            assert ret == 0
            raw = probe.run([request(1, page=page)])[0][2]
            assert raw == physical[page], 'independent physical encoding mismatch'
            for kind in ('pio', 'dma', 'mtd'):
                ret, _, read = probe.run([read_request(kind, page)])[0]
                assert ret >= 0 and read[:2048] == data[:2048]
        if a.followup_matrix:
            profiles = ((rep, delay, 449, 'dma', variant, (count, flags))
                        for rep, delay, (count, flags), variant in itertools.product(
                            range(a.repeat), (1, 10),
                            ((0, 0), (0, 1), (0, 2), (4, 1), (64, 1), (1024, 1)), (0, 1)))
        elif a.bus_reset_matrix:
            profiles = ((*values, None) for values in itertools.product(
                range(a.repeat), (1, 10), (449,), ('raw', 'dma', 'mtd'), (0, 1, 6)))
        else:
            profiles = ((*values, None) for values in itertools.product(
                range(a.repeat), (0, 1, 10), (448, 449), ('raw', 'pio', 'dma', 'mtd'), (0, 1)))
        for index, (repeat, delay, target, kind, variant, followup) in enumerate(profiles):
            prime = probe.run([read_request('dma', 450)])[0]
            assert prime[2][:2048] == logical[450][:2048]
            commands = [request(20, page=448, seed=SEEDS[0], chunk=delay, flags=variant)]
            if followup is not None:
                commands.append(request(26, page=target, length=2048,
                                        chunk=followup[0], flags=followup[1]))
            commands += [read_request(kind, target), read_request(kind, target), request(24)]
            results = probe.run(commands, accept_errors=True)
            rec, (first, second, retained) = results[0], results[-3:]
            assert rec[0] == 0 and rec[1][8:10] == [1, 1] and rec[1][12] == 1
            expected = physical[target] if kind == 'raw' else logical[target][:2048]
            observations = []
            for ret, regs, data in (first, second):
                n = len(expected)
                observations.append(dict(result=ret, regs=regs, data=data[:n].hex(),
                    matches=data[:n] == expected,
                    differences=sum(x != y for x, y in zip(data[:n], expected))))
            row = dict(index=index, repeat=repeat, delay_us=delay, target=target,
                reader=kind, abort=variant != 0, variant=variant, abort_regs=rec[1],
                abort_data=rec[2][:2048].hex(), reads=observations,
                old_buffer_changed=retained[2][:2048] != rec[2][:2048],
                old_buffer_guards=retained[1][2])
            if followup is not None:
                diag = results[1]
                assert diag[0] == 0 and diag[1][2] == 0
                row['followup'] = dict(count=followup[0], flags=followup[1],
                    result=diag[0], regs=diag[1], sram=diag[2][:2048].hex())
            report['rows'].append(row)
            print(index, kind, target, variant, delay, followup,
                  [(r['result'], r['matches'], r['differences']) for r in observations], flush=True)
            if index % 24 == 23:
                path.write_text(json.dumps(report, indent=2) + '\n')
    finally:
        path.write_text(json.dumps(report, indent=2) + '\n')
        restore(probe, directory)
        report['restored'] = True
        path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
