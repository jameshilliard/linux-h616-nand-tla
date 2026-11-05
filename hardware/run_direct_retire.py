#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""READ-ONLY streaming-DMA reset/reuse experiment on the restricted H616 rig.

Run in independently booted RAM with UBI detached and allow_write=0. Raw
flash contents stay in the private evidence directory. The JSON records
register observations and comparisons, never flash bytes or their hashes.
"""
import argparse
import itertools
import json
from pathlib import Path

if __package__:
    from .run_probe import Probe, RAW, request
else:
    from run_probe import Probe, RAW, request


def check_row(row):
    regs = row['regs']
    assert len(regs) == 16 and row['mode'] in range(4), row
    assert row['result'] == 0, row
    assert regs[6:8] == [1, 1] and regs[12] == 1, row
    assert regs[8:12] == [int(row['mode'] != 3)] * 4, row
    assert regs[15] == (0xd100 | row['mode']), row
    assert regs[13] & 6 == 6 and not regs[13] & 24, row
    assert row['next_matches'] and row['raw_followup_matches'], row


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--repeat', type=int, default=8)
    p.add_argument('--ram-only-confirmed', action='store_true', required=True)
    p.add_argument('--smoke', action='store_true')
    p.add_argument('command', nargs=argparse.REMAINDER)
    a = p.parse_args()
    directory = a.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]):
        p.error('private backups must be outside the public repository')
    if not a.command or not 1 <= a.repeat <= 100:
        p.error('transport and bounded repeat required')
    path = directory / ('direct-smoke.json' if a.smoke else 'direct-retire.json')
    if path.exists():
        p.error('refusing to replace evidence')
    probe = Probe(a.command)
    assert probe.run([request(0)])[0][1][:7] == [2048, 128, 131072, 1024, 32, 56, 2]
    baseline = b''.join((directory / f'nfc-spl-{n}.before').read_bytes()
                        for n in range(4))
    assert len(baseline) == 512 * RAW
    for chunk in (512, 1024):
        observed = b''.join(row[2] for row in probe.run(
            [request(1, page=n, chunk=chunk) for n in range(512)]))
        assert observed == baseline, 'independent raw preflight mismatch'
    report = dict(schema=1, read_only=True, rows=[], unchanged=False)
    trials = [(0, 0, 0, 448, 449)] if a.smoke else list(itertools.product(
        range(a.repeat), (0, 1, 2), (0, 1, 2, 5, 10, 20, 50, 1000), (448,), (448, 449)))
    if not a.smoke:
        trials += [(n, 3, 0, 448, 449) for n in range(a.repeat)]
    for repetition, mode, delay, page, next_page in trials:
        result, regs, data = probe.run([request(28, page=page, length=2048,
            chunk=delay, seed=next_page, flags=mode)], accept_errors=True)[0]
        row = dict(repetition=repetition, mode=mode, delay_us=delay,
                   same_page=page == next_page, result=result, regs=regs,
                   next_matches=mode == 3 or data[:2048] ==
                       baseline[next_page * RAW:next_page * RAW + 2048],
                   raw_followup_matches=False)
        report['rows'].append(row)
        path.write_text(json.dumps(report, indent=2) + '\n')
        # A poisoned engine must not be touched again. Keep all allocations.
        assert result == 0, row
        raw = probe.run([request(1, page=next_page)])[0][2]
        row['raw_followup_matches'] = raw == baseline[next_page * RAW:(next_page + 1) * RAW]
        path.write_text(json.dumps(report, indent=2) + '\n')
        check_row(row)
        print(len(report['rows']), 'mode', mode, 'delay', delay,
              'pre-ST/count', regs[0], regs[2], 'PASS', flush=True)
    final = b''.join(row[2] for row in probe.run(
        [request(1, page=n, chunk=512) for n in range(512)]))
    report['unchanged'] = final == baseline
    path.write_text(json.dumps(report, indent=2) + '\n')
    assert report['unchanged'], 'read-only test changed raw data'


if __name__ == '__main__':
    main()
