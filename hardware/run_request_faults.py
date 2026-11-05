#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded pre-confirm write aborts and PIO timeout/reuse on scratch block 7.

Requires a RAM-only boot, no UBI, fresh independent backups, and both module
permissions. All synthetic programming is confined to pages 448..511. A
poisoned controller must be independently rebooted before restoration.
"""
import argparse
import json
from pathlib import Path

from .run_probe import Probe, RAW, request, verified_backup, restore, page_image
from .run_native_direct import expected_pio_parity

DELAYS = (0, 1, 2, 5, 10, 20, 50, 1000)


def s32(value):
    return value - (1 << 32) if value & (1 << 31) else value


def check_write(row):
    r = row['regs']
    assert row['result'] == -110, row
    assert r[3:5] == [0, 1], row  # No confirm; real caller buffer.
    assert r[5:9] == [0, 0, 0, 0], row  # MDMA idle now and after observation.
    assert r[9:] == [1, 0, 0, 1, 1, 1, 0xd300], row
    assert row['unchanged'], row
    assert row['write_ok'] and row['raw_matches'] and row['pio_matches'] and row['dma_matches'], row


def check_pio(row):
    r = row['regs']
    assert row['result'] >= 0 and row['matches'], row
    assert s32(r[0]) in (0, -110), row
    assert r[7] == r[8] == 0 and r[9] & 1, row
    if s32(r[0]) == 0:
        assert r[9] == 7, row  # Both first-page data and OOB, not just follow-up.
    dma = row['flags'] & 1
    assert r[6] == dma and r[10:14] == [2 - dma, 0, dma, dma], row
    assert r[14] == 1 and r[15] == 0xd400 | row['stage'], row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--ram-only-confirmed', action='store_true', required=True)
    parser.add_argument('--repeat', type=int, default=4)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]):
        parser.error('private evidence must be outside the public repository')
    if not args.command or not 1 <= args.repeat <= 8:
        parser.error('transport and bounded repeat required')
    path = directory / 'request-faults.json'
    if path.exists():
        parser.error('refusing to replace evidence')
    backup = verified_backup(directory)
    probe = Probe(args.command)
    assert probe.run([request(0)])[0][1][:7] == [2048, 128, 131072, 1024, 32, 56, 2]
    actual = b''.join(row[2] for row in probe.run([request(1, page=p) for p in range(448, 512)]))
    assert actual == backup[448 * RAW:], 'scratch changed since verified backup'
    # Kernel checks must reject new operations outside their bounded ranges.
    assert probe.run([request(30, page=0, length=2048)], True)[0][0] == -30
    assert probe.run([request(31, page=448, column=0, length=2048, seed=1)], True)[0][0] == -1
    report = dict(schema=1, write=[], pio=[], restored=False, complete=False)
    poisoned = False

    def save():
        path.write_text(json.dumps(report, indent=2) + '\n')

    def observe(req):
        nonlocal poisoned
        result = probe.run([req], accept_errors=True)[0]
        if result[1][15] & (1 << 31):
            poisoned = True
            report['poisoned_observation'] = dict(result=result[0], regs=result[1])
            save()
            raise RuntimeError('controller quarantined; reboot before restoration')
        return result

    reference = {}
    save()
    try:
        probe.run([request(4)])
        assert all(row[2] == b'\xff' * RAW for row in probe.run(
            [request(1, page=p) for p in range(448, 512)]))
        for index in range(args.repeat * len(DELAYS)):
            page = 448 + index
            delay = DELAYS[index % len(DELAYS)]
            logical = bytes((i * 37 + (i >> 8) * 13 + index * 17 + 5) & 255 for i in range(RAW))
            ret, regs, _ = observe(request(30, page=page, length=2048, chunk=delay, data=logical))
            row = dict(page_in_block=index, delay_us=delay, result=ret, regs=regs)
            report['write'].append(row)
            save()
            assert ret == -110 and regs[3] == 0, row
            raw = probe.run([request(1, page=page)])[0][2]
            row['unchanged'] = raw == b'\xff' * RAW
            assert row['unchanged'], row
            ret, _, _ = observe(request(11, page=page, length=2048, seed=1, data=logical))
            row['write_ok'] = ret == 0
            assert row['write_ok'], row
            raw = probe.run([request(1, page=page)])[0][2]
            row['raw_matches'] = raw == page_image(logical, page)
            pio, dma = [observe(request(10, page=page, length=2048, seed=mode))
                        for mode in (0, 1)]
            row['dma_matches'] = dma[0] >= 0 and dma[1][1] == 0 and dma[2][:2056] == logical[:2056] and dma[2][-8:] == logical[-8:]
            row['pio_matches'] = pio[0] >= 0 and pio[1][1] == 0 and pio[2] == expected_pio_parity(dma[2], page)
            reference[page] = (pio[2], dma[2])
            save()
            check_write(row)
            print('write', index, 'delay', delay, 'pre-ST/count', regs[0], regs[2], 'PASS', flush=True)
        for repeat in range(args.repeat):
            for stage in (1, 2):
                for flags in (0, 1, 2, 3):
                    for delay in DELAYS:
                        page, follow = ((448, 449) if repeat % 2 == 0 else (449, 448))
                        ret, regs, data = observe(request(31, page=page, column=follow,
                            length=2048, chunk=delay, seed=stage, flags=flags, data=reference[page][0]))
                        row = dict(repetition=repeat, stage=stage, flags=flags, delay_us=delay,
                                   result=ret, regs=regs, matches=data == reference[follow][flags & 1])
                        report['pio'].append(row)
                        save()
                        check_pio(row)
                        print('pio', len(report['pio']), 'stage', stage, 'flags', flags,
                              'first', s32(regs[0]), 'pre/exit-ST', regs[1], regs[4], 'PASS', flush=True)
        report['complete'] = True
    finally:
        save()
        if not poisoned:
            restore(probe, directory)
            report['restored'] = True
            save()


if __name__ == '__main__':
    main()
