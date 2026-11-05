#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline checks for the read-only streaming-DMA ownership experiment.

Only comparison results are public: replay checks evidence structure and
observations, not original flash bytes. It is not an independent hardware run.
"""
import itertools
import json
from pathlib import Path

from hardware.run_direct_retire import check_row
from hardware.run_native_direct import check_observation

CAPTURE = Path(__file__).with_name('h616-direct-dma-20261104.json')


def check_raw(report):
    assert report['schema'] == 1 and report['read_only'] is True
    assert report['unchanged'] is True
    profiles = list(itertools.product(range(8), (0, 1, 2),
        (0, 1, 2, 5, 10, 20, 50, 1000), (True, False)))
    profiles += [(n, 3, 0, False) for n in range(8)]
    assert len(report['rows']) == len(profiles) == 392
    active = {1: 0, 2: 0}
    for row, profile in zip(report['rows'], profiles):
        assert (row['repetition'], row['mode'], row['delay_us'], row['same_page']) == profile
        check_row(row)
        if row['mode'] in active and row['regs'][0] & 24:
            active[row['mode']] += 1
        if row['mode'] in (0, 3):
            assert row['regs'][0] & 6 == 6 and not row['regs'][0] & 24
    assert all(active.values()), 'both abort variants need active-engine witnesses'


def check_native(report):
    assert report['schema'] == 1 and report['read_only'] is True
    assert report['programmed_and_erased_controls'] is True
    assert report['path_specific_oob_verified'] is True
    pages = report['selected_pages_in_block']
    assert len(pages) == len(set(pages)) == 2 and all(p in range(4) for p in pages)
    controls = list(itertools.product(range(4), range(3)))
    for key in ('baseline', 'final'):
        assert len(report[key]) == len(controls)
        for row, profile in zip(report[key], controls):
            assert (row['page_in_block'], row['mode']) == profile
            check_observation(row)
    profiles = list(itertools.product(range(8), (0, 1, 2, 5, 10, 20, 50, 1000), pages))
    assert len(report['rows']) == len(profiles) == 128
    active = 0
    for trial, (repeat, delay, page) in zip(report['rows'], profiles):
        row, followup = trial['abort'], trial['followup']
        assert (row['repetition'], row['delay_us'], row['page_in_block']) == (repeat, delay, page)
        assert row['mode'] == 3 and followup['mode'] == 2
        assert followup['page_in_block'] in pages and followup['page_in_block'] != page
        check_observation(row)
        check_observation(followup)
        active += bool(row['regs'][2] & 24)
    assert active, 'native injected faults need active-engine witnesses'


def check(capture):
    assert capture['schema'] == 1
    check_raw(capture['raw'])
    check_native(capture['native'])
    stress = capture['ecc_stress']
    assert stress['schema'] == 1 and stress['read_only'] is True
    assert stress['rounds'] == 100 and stress['abort_delay_us'] == 5
    assert stress['page_in_block'] == 0 and len(stress['rows']) == 400
    for row, profile in zip(stress['rows'], itertools.product(range(100), range(4))):
        assert (row['repetition'], row['mode']) == profile
        check_observation(row)
        assert row['result'] == 0 and row['regs'][:2] == [0, 0]
    final = capture['final_verification']
    assert final['all_spl_unchanged'] is True
    assert final['ubi_block_unchanged'] is True
    assert final['allow_write_disabled'] is True
    assert final['module_unloaded'] is True
    assert final['ordinary_mtd_read_passed'] is True
    assert final['normal_boot_restored'] is True


def replay(path=CAPTURE):
    check(json.loads(path.read_text()))
    print('Streaming-DMA capture: 392 raw retirement/control trials, '
          '128 native ECC abort/retries, 152 native control/follow-up reads, '
          'and 400 outlier-profile stress reads passed')


if __name__ == '__main__':
    replay()
