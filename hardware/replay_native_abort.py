#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check synthetic observations from the actual driver's abort/retry path."""
import hashlib
import itertools
import json
from pathlib import Path

from hardware.run_probe import RAW, page_image

MEASURED_SOURCE = '42b063bbdafd7ca5b1b592c411df84c8a7fcd7d2896b279c78775b195572df7a'
FINAL_SOURCE = '7f93090564302e65e251c48d7a700ec4d20b1b46e2a29d4f777e747f638348cc'
CAPTURE = Path(__file__).with_name('h616-native-abort-20261104.json')


def check(capture):
    assert capture['schema'] == 1 and capture['measured_source_sha256'] == MEASURED_SOURCE
    assert capture['final_source_sha256'] == FINAL_SOURCE
    report = capture['native_abort']
    assert report['schema'] == 1 and report['injected_timeout'] is True
    assert report['synthetic_pattern'] == 'SHAKE256(nfc-reset-cause-page-N)'
    assert report['restored'] is True
    profiles = list(itertools.product(range(6), (0, 1, 10, 1000), (448, 449),
                                      ('raw', 'pio', 'dma', 'mtd')))
    assert len(report['rows']) == len(profiles) == 192
    logical = {p: hashlib.shake_256(f'nfc-reset-cause-page-{p}'.encode()).digest(RAW)
               for p in (448, 449)}
    physical = {p: page_image(data, p) for p, data in logical.items()}
    for row, profile in zip(report['rows'], profiles):
        assert (row['repeat'], row['delay_us'], row['next_page'], row['reader']) == profile
        page = row['next_page']
        followup = physical[page] if row['reader'] == 'raw' else logical[page][:2048]
        expected = (logical[448][:2048], followup, followup)
        assert len(row['observations']) == 3
        for observation, data in zip(row['observations'], expected):
            assert observation['result'] == 0 and observation['matches'] is True
            assert observation['sha256'] == hashlib.sha256(data).hexdigest()
            assert len(observation['regs']) == 16
        first = row['observations'][0]['regs']
        assert first[:2] == [0, 0]  # No corrected or failed ECC in recovered data.
        # One PIO retry, no PIO write, one DMA prepare/cleanup, caller guards.
        assert first[10:16] == [1, 0, 1, 1, 1, 16]
    stress = capture['stress']
    assert stress['phase'] == 'stress' and stress['restored'] is True
    assert len(stress['rows']) == 20
    for batch, row in enumerate(stress['rows']):
        assert row['batch'] == batch and row['reads'] == 100
        assert all(row[name] == 0 for name in
                   ('bad_data', 'bad_guards', 'fallback', 'failed_ecc'))
    final = capture['final_verification']
    assert final['all_spl_restored'] is True and final['module_unloaded'] is True
    assert final['ordinary_mtd_read_passed'] is True
    assert final['ordinary_mtd_read_bytes'] == 32 * 1024 * 1024


def replay(path=CAPTURE):
    check(json.loads(path.read_text()))
    print('Native abort capture: 192 recoveries / 576 read observations, '
          '2000 stress reads and final restoration checks passed')


if __name__ == '__main__':
    replay()
