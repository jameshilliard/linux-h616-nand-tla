# SPDX-License-Identifier: GPL-2.0-or-later
"""Replay synthetic reset captures against independent byte-level oracles.

This preserves observed failures, including silent corruption. A passing replay
means that the interpretation matches the capture, not that soft reset works.
"""
import hashlib
import itertools
import json
from pathlib import Path

from hardware.run_probe import RAW, page_image


def replay(path):
    capture = json.loads(Path(path).read_text())
    assert capture['schema'] == 1
    blobs = {key: bytes.fromhex(value) for key, value in capture['blobs'].items()}
    for key, data in blobs.items():
        assert hashlib.sha256(data).hexdigest() == key
        assert len(data) in (2048, RAW)
    used = set()

    def data(value):
        assert set(value) == {'blob_sha256'}
        key = value['blob_sha256']
        used.add(key)
        return blobs[key]

    # Do not depend on the live test runner's pattern() implementation.
    logical = {page: hashlib.shake_256(f'nfc-reset-cause-page-{page}'.encode()).digest(RAW)
               for page in (448, 449)}
    physical = {page: page_image(value, page) for page, value in logical.items()}
    summary = {}
    observations = 0
    for name, expected_count in (('reset-diagnosis', 144), ('reset-followup', 144), ('reset-bus', 108)):
        report = capture['reports'][name]
        assert report['restored'] and len(report['rows']) == expected_count
        profiles = set()
        silent, rotations, sram_spill = 0, 0, 0
        for index, row in enumerate(report['rows']):
            assert row['index'] == index and row['reader'] in ('raw', 'pio', 'dma', 'mtd')
            variant = row.get('variant', int(row['abort']))
            assert variant in (0, 1, 6) and row['abort'] == (variant != 0)
            f = row.get('followup')
            operation = (f['count'], f['flags']) if f else None
            profile = (row['repeat'], row['delay_us'], row['target'], row['reader'], variant, operation)
            assert profile not in profiles
            profiles.add(profile)
            assert row['abort_regs'][8:10] == [1, 1] and row['abort_regs'][12] == 1
            assert not row['old_buffer_changed'] and row['old_buffer_guards'] == 1
            assert len(data(row['abort_data'])) == 2048
            expected = physical[row['target']] if row['reader'] == 'raw' else logical[row['target']][:2048]
            assert len(row['reads']) == 2
            for ordinal, read in enumerate(row['reads']):
                actual = data(read['data'])
                assert len(actual) == len(expected)
                assert read['matches'] == (actual == expected)
                assert read['differences'] == sum(a != b for a, b in zip(actual, expected))
                if ordinal or variant in (0, 6):
                    assert actual == expected and read['result'] == 0
                if row['reader'] in ('pio', 'dma'):
                    assert read['regs'][14:] == [1, 16]
                    assert read['regs'][10:14] == ([1, 0, 0, 0] if row['reader'] == 'pio' else [0, 0, 1, 1])
                if actual != expected:
                    assert ordinal == 0 and variant == 1
                    assert actual[1024:] == expected[1024:]
                    if row['reader'] != 'raw' and read['regs'][1] == 0:
                        assert read['result'] == 0
                        silent += 1
                    if any(actual[:1024] == expected[n:1024] + expected[:n] for n in (972, 976)):
                        rotations += 1
                observations += 1
            if f:
                assert f['result'] == 0 and f['regs'][2] == 0
                sram = data(f['sram'])
                assert len(sram) == 2048
                if f['flags'] & 1:
                    count = f['count']
                    if not count:
                        assert sram == b'\xa5' * 2048
                    elif variant == 0:
                        assert sram == physical[row['target']][:count] + b'\xa5' * (2048 - count)
                    elif sram[count:] != b'\xa5' * (2048 - count):
                        sram_spill += 1
                if f['count']:
                    assert row['reads'][0]['matches']  # Observed, not a universal drain guarantee.
                observations += 1
        if name == 'reset-diagnosis':
            expected_profiles = {(*v, None) for v in itertools.product(
                range(3), (0, 1, 10), (448, 449), ('raw', 'pio', 'dma', 'mtd'), (0, 1))}
        elif name == 'reset-followup':
            expected_profiles = {(rep, delay, 449, 'dma', variant, op)
                for rep, delay, variant, op in itertools.product(range(6), (1, 10), (0, 1),
                    ((0, 0), (0, 1), (0, 2), (4, 1), (64, 1), (1024, 1)))}
        else:
            expected_profiles = {(*v, None) for v in itertools.product(
                range(6), (1, 10), (449,), ('raw', 'dma', 'mtd'), (0, 1, 6))}
        assert profiles == expected_profiles
        summary[name] = dict(trials=expected_count, failures=sum(not r['reads'][0]['matches'] for r in report['rows']),
                             silent_ecc_failures=silent, exact_rotations=rotations, sram_spill=sram_spill)
    assert summary['reset-diagnosis']['failures'] == 20
    assert summary['reset-diagnosis']['silent_ecc_failures'] == 7
    assert summary['reset-diagnosis']['exact_rotations'] >= 2
    assert summary['reset-followup']['failures'] == 17
    assert summary['reset-followup']['sram_spill'] > 0
    assert summary['reset-bus']['failures'] == 19
    assert used == set(blobs)
    final = capture['final_verification']
    assert all(final[k] for k in ('all_spl_restored', 'module_unloaded', 'ordinary_mtd_read_passed'))
    print(f'{observations} reset observations replayed across 396 trials; observed corruption remains explicit')
    print(json.dumps(summary, sort_keys=True))
    return summary
