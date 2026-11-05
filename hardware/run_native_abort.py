#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise candidate-driver abort/retry, not the diagnostic reset helper.

Only read transfers are interrupted. Erase/program operations prepare and
restore the authorized block 7. Requires fresh off-board backups and RAM boot.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path

from run_probe import Probe, RAW, request, verified_backup, restore, page_image
from run_reset_diagnosis import pattern, read_request
from run_extended import native, checked_native, NWRITE


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--repeat', type=int, default=6)
    p.add_argument('--ram-only-confirmed', action='store_true', required=True)
    p.add_argument('command', nargs=argparse.REMAINDER)
    a = p.parse_args()
    directory = a.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]):
        p.error('keep private backups outside the public repository')
    if not a.command or not 1 <= a.repeat <= 100:
        p.error('transport and bounded repeat count required')
    path = directory / 'native-abort.json'
    if path.exists():
        p.error('refusing to replace evidence')
    probe = Probe(a.command)
    backup = verified_backup(directory)
    assert b''.join(r[2] for r in probe.run([
        request(1, page=n) for n in range(448, 512)])) == backup[448 * RAW:]
    logical = {n: pattern(n) for n in (448, 449, 450)}
    physical = {n: page_image(data, n) for n, data in logical.items()}
    report = dict(schema=1, synthetic_pattern='SHAKE256(nfc-reset-cause-page-N)',
                  injected_timeout=True, rows=[], restored=False)
    try:
        probe.run([request(4)])
        for page, data in logical.items():
            assert checked_native(probe, native(NWRITE, page=page, data=data, flags=0))[0] == 0
            assert probe.run([request(1, page=page)])[0][2] == physical[page]
        for repeat, delay, page, kind in itertools.product(
                range(a.repeat), (0, 1, 10, 1000), (448, 449), ('raw', 'pio', 'dma', 'mtd')):
            assert probe.run([read_request('dma', 450)])[0][2][:2048] == logical[450][:2048]
            results = probe.run([request(27, page=448, length=2048, chunk=delay),
                                 read_request(kind, page), read_request(kind, page)],
                                accept_errors=True)
            expected = [logical[448][:2048]] + [physical[page] if kind == 'raw' else logical[page][:2048]] * 2
            rows = [dict(result=ret, regs=regs, matches=data[:len(want)] == want,
                         sha256=hashlib.sha256(data[:len(want)]).hexdigest())
                    for (ret, regs, data), want in zip(results, expected)]
            report['rows'].append(dict(repeat=repeat, delay_us=delay, next_page=page,
                                       reader=kind, observations=rows))
            path.write_text(json.dumps(report, indent=2) + '\n')
            assert all(r['result'] >= 0 and r['matches'] for r in rows), report['rows'][-1]
            # PIO retry once, one DMA prepare and cleanup, original caller guards intact.
            assert rows[0]['regs'][10:16] == [1, 0, 1, 1, 1, 16], rows[0]
            assert rows[0]['regs'][:2] == [0, 0], rows[0]
            print(len(report['rows']), delay, page, kind, 'PASS', flush=True)
    finally:
        path.write_text(json.dumps(report, indent=2) + '\n')
        restore(probe, directory)
        report['restored'] = True
        path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
