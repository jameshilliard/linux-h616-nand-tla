#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only repetition of the native matrix's corrected-ECC outlier profile."""
import argparse
import json
from pathlib import Path

from run_probe import Probe, RAW, request
from run_native_direct import check_observation, expected_pio_parity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--ram-only-confirmed', action='store_true', required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]) or not args.command:
        parser.error('private evidence directory and transport required')
    output = directory / 'direct-ecc-stress.json'
    if output.exists():
        parser.error('refusing to replace evidence')
    probe = Probe(args.command)
    raw = (directory / 'nfc-ubi-block.before').read_bytes()
    assert len(raw) == 64 * RAW
    baseline = probe.run([request(29, page=2688, length=2048, chunk=0, seed=mode)
                          for mode in (0, 1)])
    assert all(ret >= 0 and not regs[1] for ret, regs, data in baseline)
    pio, dma = [row[2] for row in baseline]
    assert dma[2056:2168] == raw[2056:2168]
    assert expected_pio_parity(dma, 2688) == pio
    report = dict(schema=1, read_only=True, page_in_block=0,
                  rounds=100, abort_delay_us=5, rows=[])
    for batch in range(10):
        rows = probe.run([request(29, page=2688, length=2048,
                                 chunk=5 if mode == 3 else 0, seed=mode)
                          for repeat in range(10) for mode in range(4)], accept_errors=True)
        for index, (ret, regs, data) in enumerate(rows):
            mode = index % 4
            row = dict(repetition=batch * 10 + index // 4, mode=mode,
                       result=ret, regs=regs, matches=data == (pio if mode in (0, 3) else dma))
            report['rows'].append(row)
            output.write_text(json.dumps(report, indent=2) + '\n')
            check_observation(row)
        print(len(report['rows']), 'reads; corrected observations',
              sum(bool(row['regs'][0]) for row in report['rows']), flush=True)


if __name__ == '__main__':
    main()
