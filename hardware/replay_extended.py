# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent replay of extended synthetic captures; never accesses hardware."""
from collections import Counter
import json
from pathlib import Path

from codec_oracle import REVERSE, scramble
from hardware.run_probe import RAW, SEEDS, measured_stream, page_image
from hardware.run_extended import STRENGTHS, digest, logical


def toggle_bytes(plain, seed, chunk, read, page):
    key = scramble(bytes(len(plain)), seed)
    output, phase = bytearray(), 0
    for command, offset in enumerate(range(0, len(plain), chunk)):
        part = plain[offset:offset + chunk]
        if command == 1:
            output.extend(part)
            continue  # Disabled transfer does not advance the randomizer.
        start = phase if read and page else 0
        mask = bytearray(key[start:start + len(part)])
        if read and len(part) % 4:
            mask[-1] = 0
        output.extend(x ^ y for x, y in zip(part, mask))
        phase += len(part)
    return bytes(output)


def replay(path):
    c = json.loads(Path(path).read_text())
    checks = 0
    for value in c.values():
        if isinstance(value, dict) and 'restored' in value:
            assert value['restored']
    assert len(c['native']['rows']) == 64
    for row in c['native']['rows']:
        index = row['index']
        expected = page_image(logical(index), 448 + index)
        assert row['physical_matches_oracle'] and row['physical_sha256'] == digest(expected)
        assert row['write_counts'] == ([0, 1, 0, 0] if row['writer'] == 0 else [0, 0, 1, 1])
        for read in row['reads']:
            assert all(read[k] for k in ('main_matches', 'user_matches', 'tail_matches'))
            assert read['regs'][:2] == [0, 0] and read['regs'][14:] == [1, 16]
            assert read['regs'][10:14] == ([1, 0, 0, 0] if read['mode'] == 0 else [0, 0, 1, 1])
            checks += 1
    for row in c['context']['rows']:
        assert row['write_regs'][7:10] == [0, 0, 0]
        for read in row['reads']:
            assert read['regs'][7:10] == [0, 0, 0]
            assert all(read[k] for k in ('main_matches', 'user_matches', 'tail_matches'))
            checks += 1
    assert len(c['codewords']['rows']) == 59
    for index, row in enumerate(c['codewords']['rows']):
        if not row['randomized']:
            assert row['regs'][2] & 1 and row['regs'][3] & 1 == (row['errors'] == 255)
        elif row['errors'] <= row['strength']:
            assert row['regs'][:2] == [0, row['errors']]
            assert row['main_matches'] and row['user_matches']
            assert row['decoded_sha256'] == digest(logical(index)[:1024])
        else:
            assert row['regs'][0] & 1  # Only these selected over-limit patterns.
        checks += 1
    assert Counter(row['strength'] for row in c['codewords']['rows'][:45]) == Counter({t: 3 for t in STRENGTHS})
    assert len(c['eccregions']['rows']) == 53
    for index, row in enumerate(c['eccregions']['rows']):
        assert row['regs'][:2] == [0, 1]
        assert row['main_matches'] and row['user_matches']
        assert row['decoded_sha256'] == digest(logical(index)[:1024])
        checks += 1
    assert len(c['streams']['rows']) == len(SEEDS) * 12 * 4
    for row in c['streams']['rows']:
        col, length, flags = row['column'], row['length'], row['flags']
        plain = logical(0)[col:col + length]
        expected = measured_stream(plain, row['seed'], 31, True, bool(flags & 4))
        if flags & 32:
            key = bytes(x ^ y for x, y in zip(plain, expected)).translate(REVERSE)
            expected = bytes(x ^ y for x, y in zip(plain, key))
        assert row['matches'] and row['sha256'] == digest(expected)
        checks += 1
    for read, rows in ((True, c['streams']['transitions']), (False, c['transitionwrites']['rows'])):
        assert len(rows) == 24
        for row in rows:
            expected = toggle_bytes(logical(0)[:4 * row['chunk']], row['seed'], row['chunk'], read, bool(row['flags'] & 4))
            assert expected.hex() == row['data_hex']
            if not read:
                assert row['tail_erased']
            checks += 1
    # Generic helpers are not the installed Allwinner subpage callback. Retain
    # their original diagnostic disagreements rather than deleting them.
    mismatches = 0
    for row in c['callers']['rows']:
        assert row['raw_callback_matches']
        if row['op'] == 17:
            assert row['raw_write_matches']
        else:
            assert row['oob_callback_matches']
            for read in row['reads']:
                assert read['regs'][14:] == [1, 16]
                if not read['matches']:
                    assert row['op'] == 13
                    mismatches += 1
        checks += 1
    assert mismatches > 0
    for row in c['subpolicy']['rows']:
        failed = {'valid': 0, 'erased': 0, 'zero': 2, 'erased-first': 0, 'erased-second': 1}[row['kind']]
        assert row['full_regs'][1] == failed
        for read in row['reads']:
            assert read['matches'] and read['public_matches']
            assert read['regs'][14:] == [1, 17]
            assert read['public_result'] == (-74 if failed else 0)
            checks += 1
    # Follow-up corrects the first report's no-OOB comparison and explicitly
    # compares AUTO/RAW returned bytes, rather than its former tautology.
    for index, row in enumerate(c['mtdoob']['rows']):
        if row['length'] == 1024:
            assert row['write_result'] == -22 and not row['reads']
            continue
        assert row['write_result'] == 0
        for read in row['reads']:
            assert read['result'] == 0 and read['main_matches'] and read['untouched_user_tail']
            olen = read['ooblen']
            if not olen:
                assert read['oob_matches'] is None and not read['oob_hex']
            else:
                actual = bytes.fromhex(read['oob_hex'])
                expected = logical(index)[2048:2048 + olen]
                assert read['oob_matches']
                if row['mode'] == 0:
                    assert actual[:8] == expected[:8] and actual[120:] == expected[120:]
                else:
                    assert actual == expected
            checks += 1
    assert sum(row['reads'] for row in c['stress']['rows']) == 2000
    for row in c['stress']['rows']:
        assert all(row[k] == 0 for k in ('bad_data', 'bad_guards', 'fallback', 'failed_ecc'))
        checks += row['reads']
    bad_after_abort = 0
    for row in c['recovery']['rows']:
        assert row['result'] == 0 and row['regs'][8:10] == [1, 1] and row['regs'][12] == 1
        if row['mode'] != 1:
            assert row['completed_data_matches'] and row['subsequent_read_matches']
        elif not row['subsequent_read_matches']:
            bad_after_abort += 1
        if row['mode'] == 2:
            assert row['regs'][3] & 6 == 6 and row['regs'][4] & 6 == 0 and row['regs'][5] == 0
        checks += 1
    assert bad_after_abort > 0  # Known finding must not disappear from the record.
    for name in ('abort-followup', 'reset-variants'):
        failures = set()
        for row in c[name]['rows']:
            assert row['initial_regs'][8:10] == [1, 1]
            for n, read in enumerate(row['reads']):
                assert not read['old_buffer_changed'] and read['old_buffer_guards'] == 1
                if not read['matches']:
                    assert n == 0
                    failures.add(row.get('variant', 1))
                checks += 1
        assert failures == ({1, 3, 4, 5} if name == 'reset-variants' else {1})
    for row in c['reset-variants']['masked_native_reads']:
        assert row['matches'] and row['result'] == 0 and row['regs'][14:] == [1, 16]
        assert row['regs'][10:14] == ([1, 0, 0, 0] if row['mode'] == 0 else [0, 0, 1, 1])
        checks += 1
    assert c['final_verification']['all_spl_restored']
    assert c['final_verification']['module_unloaded']
    print(f'{checks} extended hardware observations replayed; reset anomaly explicitly retained')
