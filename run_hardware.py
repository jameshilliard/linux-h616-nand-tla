#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline replay of sanitized real-H616 observations; does not access a board."""
import hashlib
import json
from pathlib import Path
import unittest

from hardware.run_probe import RAW, SEEDS, measured_stream, page_image
from hardware.test_safety import BackupSafety
from hardware.test_reset import ResetEvidence
from hardware.test_native_abort import NativeAbortEvidence
from hardware.test_direct import DirectEvidence
from hardware.test_streaming_implementation import StreamingImplementationEvidence
from hardware.test_request_faults import RequestFaultEvidence
from codec_oracle import scramble
from hardware.replay_extended import replay
from hardware.replay_reset import replay as replay_reset
from hardware.replay_native_abort import replay as replay_native_abort
from hardware.replay_direct import replay as replay_direct
from hardware.streaming_implementation import CAPTURE as STREAMING_CAPTURE, check as check_streaming
from hardware.request_faults_evidence import CAPTURE as REQUEST_CAPTURE, check as check_requests

HERE = Path(__file__).resolve().parent


def main():
    capture = json.loads((HERE / 'hardware/h616-capture-20261104.json').read_text())
    checks = 0
    for row in capture['counter']:
        assert row['readback'] == row['written'] & 1023
        assert row['expected_length'] == (row['readback'] or 1024)
        assert row['prefix_matches'] and row['tail_untouched']
        checks += 1
    assert capture['sram_banks_independent']
    for seed, chunk, flags, digest in capture['read_matrix']:
        actual = measured_stream(bytes(RAW), seed, chunk, True, bool(flags & 4))
        assert hashlib.sha256(actual).hexdigest() == digest
        checks += 1
    for row in capture['tail_access']['rows']:
        size = row['size']
        assert row['differences'] == ([size - 1] if size % 4 else [])
        checks += 1
    before, zero, other, cleared = capture['tail_access']['events']
    assert before & 2 and zero == before and other & 2 and not cleared & 2
    for row in capture['native_ecc']:
        index = row['index']
        logical = bytes((i * 37 + (i >> 8) * 13 + index * 7) & 255 for i in range(RAW))
        physical = page_image(logical, 448 + index)
        assert hashlib.sha256(physical).hexdigest() == row['physical_sha256']
        assert row['physical_matches_oracle'] and row['protected_matches'] and row['tail_matches']
        assert row['return'] == 0 and row['regs'][:2] == [0, 0]
        assert all(p['data_matches'] and p['user_matches'] and p['regs'][:4] == [0] * 4
                   for p in row['pio'])
        checks += 1
    for row in capture['software_codewords']:
        count, region = row['injected_bitflips'], row['region']
        if count == 65:
            # Callback status is transport success; MTD interprets failed delta.
            assert row['regs'][1] == 1 and not row['protected_matches']
            assert row['pio'][0]['regs'][0] & 1
        else:
            unprotected = region >= 2168
            expected = 0 if unprotected else count
            assert row['return'] == expected and row['regs'][:2] == [expected, 0]
            assert row['protected_matches'] and row['tail_matches'] != unprotected
            slot = 1 if region in (1024, 2112) else 0
            for i, p in enumerate(row['pio']):
                assert p['data_matches'] and p['user_matches'] and not p['regs'][0] & 1
                assert p['regs'][1] & 255 == (expected if i == slot else 0)
        checks += 1
    for index, row in enumerate(capture['raw_stream_writes']):
        flags, chunk = row['flags'], row['chunk']
        logical = bytes((i * 19 + index * 31) & 255 for i in range(RAW))
        physical = measured_stream(logical, 0x2b75, chunk, False, bool(flags & 4))
        assert hashlib.sha256(physical).hexdigest() == row['physical_sha256']
        decoded = measured_stream(physical, 0x2b75, chunk, True, bool(flags & 4))
        assert row['roundtrip'] == (decoded == logical)
        assert row['measured_write_contract'] and row['measured_read_contract']
        checks += 1
    assert capture['erased_read']['all_ff'] and capture['erased_read']['regs'][:2] == [0, 0]
    assert capture['erased_read']['regs'][3:5] == [3, 3]
    assert capture['all_spl_restored'] and capture['module_unloaded']
    print(f'{checks} captured hardware checks replayed; 72 stream digests each measured on three pages')
    tests = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(case)
                               for case in (BackupSafety, ResetEvidence, NativeAbortEvidence,
                                            DirectEvidence, StreamingImplementationEvidence,
                                            RequestFaultEvidence))
    result = unittest.TextTestRunner().run(tests)
    if not result.wasSuccessful():
        raise SystemExit(1)
    replay(HERE / 'hardware/h616-extended-20261104.json')
    replay_reset(HERE / 'hardware/h616-reset-20261104.json')
    replay_native_abort()
    replay_direct()
    check_streaming(json.loads(STREAMING_CAPTURE.read_text()))
    print('Final streaming implementation: 680 native reads, abort/retry, guards and delayed-write checks passed')
    check_requests(json.loads(REQUEST_CAPTURE.read_text()))
    print('Current source: pre-confirm write abort, PIO timeout/reuse and ordinary-kernel checks passed')
    print('No board access. Passing replay does not validate unmeasured hardware contracts.')


if __name__ == '__main__':
    main()
