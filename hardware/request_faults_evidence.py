#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Allowlisted export/replay: register observations, never existing flash data."""
import argparse
import itertools
import json
from pathlib import Path

from .run_request_faults import DELAYS, check_write, check_pio, s32

SOURCE = '102b942b18d37e37f6f6dd605233da79149e2fbff61f38daf25acde75f0ce676'
CAPTURE = Path(__file__).with_name('h616-request-faults-20261105.json')
FINAL_FIELDS = ('all_spl_restored ubi_block_unchanged diagnostic_module_unloaded '
                'ordinary_kernel_has_no_test_hooks ordinary_read_bytes ordinary_read_count '
                'ordinary_ecc_corrected_delta ordinary_ecc_failed_delta '
                'ordinary_scratch_verified normal_boot_restored')


def pick(value, fields):
    return {field: value[field] for field in fields.split()}


def check(capture):
    assert capture['schema'] == 1 and capture['source_sha256'] == SOURCE
    report = capture['observations']
    assert report['schema'] == 1 and report['complete'] and report['restored']
    assert len(report['write']) == 32
    for i, row in enumerate(report['write']):
        assert row['page_in_block'] == i and row['delay_us'] == DELAYS[i % 8]
        check_write(row)
    assert any(row['regs'][0] & 24 for row in report['write']), 'no active-write witness'
    profiles = list(itertools.product(range(4), (1, 2), range(4), DELAYS))
    assert len(report['pio']) == len(profiles) == 256
    for row, profile in zip(report['pio'], profiles):
        assert (row['repetition'], row['stage'], row['flags'], row['delay_us']) == profile
        check_pio(row)
    assert any(s32(row['regs'][0]) == -110 for row in report['pio']), 'no timeout return'
    assert any(row['regs'][4] & 24 for row in report['pio']), 'no active-at-cleanup witness'
    assert any(row['regs'][5] & 2 for row in report['pio']), 'no late-event witness'
    final = capture['final_verification']
    for field in ('all_spl_restored', 'ubi_block_unchanged', 'diagnostic_module_unloaded',
                  'ordinary_kernel_has_no_test_hooks', 'ordinary_scratch_verified',
                  'normal_boot_restored'):
        assert final[field] is True, field
    assert final['ordinary_read_bytes'] == 64 * 1024 * 1024
    assert final['ordinary_read_count'] == 3
    assert final['ordinary_ecc_corrected_delta'] >= 0
    assert final['ordinary_ecc_failed_delta'] == 0


def export(directory):
    report = json.loads((directory / 'request-faults.json').read_text())
    final = json.loads((directory / 'request-final.json').read_text())
    out = dict(schema=1, source_sha256=SOURCE,
        observations=dict(pick(report, 'schema complete restored'),
            write=[pick(row, 'page_in_block delay_us result regs unchanged write_ok '
                            'raw_matches pio_matches dma_matches') for row in report['write']],
            pio=[pick(row, 'repetition stage flags delay_us result regs matches')
                 for row in report['pio']]),
        final_verification=pick(final, FINAL_FIELDS))
    check(out)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export-directory', type=Path)
    parser.add_argument('--capture', type=Path, default=CAPTURE)
    args = parser.parse_args()
    if args.export_directory:
        if args.capture.exists():
            parser.error('refusing to replace capture')
        args.capture.write_text(json.dumps(export(args.export_directory), indent=2) + '\n')
    check(json.loads(args.capture.read_text()))
    print('Request-fault evidence: 32 pre-confirm write aborts, 256 PIO reuse trials, '
          'restoration and ordinary-kernel smoke checks passed')


if __name__ == '__main__':
    main()
