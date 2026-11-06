#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check exact LFSR arithmetic and its composition with source policy helpers.

The default retains the pinned submission's late-marker bug as a negative
control. --candidate-source requires the candidate to satisfy every policy.
MMIO and decoded NAND bytes are fixtures; this program never accesses flash.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re

from boundary_common import HERE, check
from randomizer_cases import PHASE, POLICY, case, freeze, model_records, policy_controls
from run_bbm_init import fixture
import run as tlc


def advance(seed, count):
    """Independent bit-array oracle, not a call into the driver recurrence."""
    bits = [(seed >> i) & 1 for i in range(15)]
    for _ in range(count):
        bits = bits[1:] + [int(bits[0] != bits[1])]
    return sum(bit << i for i, bit in enumerate(bits))


def seed_tables():
    pin = json.loads((HERE / 'reference/compat/source.json').read_text())
    raw = (HERE / 'reference/compat/ndfc_base.c').read_bytes()
    assert hashlib.sha256(raw).hexdigest() == pin['files']['ndfc_base.c']
    body = re.search(r'const u32 random_seed\[128\] = \{(.*?)\};', raw.decode(), re.S)[1]
    page = tuple(int(x, 16) for x in re.findall(r'0x[0-9a-f]+', body))
    assert len(page) == 128
    return dict(PageSeeds=page,
                Ecc512=tuple(advance(s, 512 * 8 + 15) for s in page),
                Ecc1024=tuple(advance(s, 1024 * 8 + 15) for s in page))


def register_contract(source):
    """Reject drift between the fixture register definitions and the source."""
    patterns = (
        r'#define\s+NFC_REG_ECC_CTL\s+0x0034\b',
        r'#define\s+NFC_RANDOM_SEED_MSK\s+GENMASK\(30,\s*16\)',
        r'#define\s+NFC_RANDOM_SEED\(x\)\s+\(\(x\)\s*<<\s*16\)',
        r'#define\s+NFC_RANDOM_EN\(nfc\)\s+\(nfc->caps->random_en_mask\)',
        r'#define\s+NFC_REG_A10_USER_DATA\s+0x0050\b',
        r'#define\s+NFC_REG_H6_USER_DATA\s+0x0080\b',
        r'#define\s+NFC_REG_H6_USER_DATA_LEN\s+0x0070\b',
    )
    for pattern in patterns:
        assert re.search(pattern, source), 'fixture/source register contract changed: ' + pattern
    for name, bit in (('a10', 9), ('a23', 9), ('h616', 5)):
        body = re.search(r'static const struct sunxi_nfc_caps sunxi_nfc_' + name +
                         r'_caps = \{(.*?)\n};', source, re.S)[1]
        assert re.search(r'\.random_en_mask\s*=\s*BIT\(' + str(bit) + r'\)', body)


def inputs(tables):
    records = []
    for hw in (0, 1):
        for step in (512, 1024):
            for ppb in (32, 64, 128, 256):
                for page in sorted({0, 1, ppb - 1, ppb, 127, 128, 255}):
                    # Include randomized -> plain, vendor -> late and late -> plain.
                    for mode in (1, 0, 3, 2, 0):
                        serial = len(records)
                        bbm = serial % 2 == 0
                        seed = tables[f'Ecc{step}'][page % min(ppb, 128)]
                        marker = [255 if serial % 3 else 0, 255 if serial % 5 else 0, 18, 52]
                        decoded = marker[:]
                        if bbm and mode in (1, 2):
                            decoded[0] ^= seed & 255
                            decoded[1] ^= advance(seed, 8) & 255
                        records.append(dict(id=serial, hw=hw, enableBit=5 if hw else 9,
                                            mode=mode, step=step, ppb=ppb, page=page,
                                            bbm=bbm, fail=-110 if serial % 3 == 1 else 0,
                                            marker=marker, input=decoded))
    return records


def header(records):
    lines = ['static const struct randomizer_input inputs[] = {']
    for r in records:
        values = [r[k] for k in ('hw', 'mode', 'step', 'ppb', 'page', 'bbm', 'fail')]
        lines.append('{' + ','.join(str(int(v)) for v in values) + ',{' +
                     ','.join(map(str, r['marker'])) + '},{' +
                     ','.join(map(str, r['input'])) + '}},')
    return '\n'.join(lines + ['};'])


def check_observations(records, tables, *, historical):
    """Also check all records without TLC, including cross-wrapper continuity."""
    previous = {0: 0x92340002, 1: 0x92340002}
    failures = []
    for r in records:
        o = r['observed']
        index = r['page'] % min(r['ppb'], 128)
        page, ecc = tables['PageSeeds'][index], tables[f"Ecc{r['step']}"][index]
        random = r['mode'] != 0
        late_bug = historical and r['mode'] == 2
        assert o['randomized'] == random
        assert o['compensate'] == (r['mode'] in (1, 2) and not late_bug)
        assert (o['pageSeed'], o['eccSeed']) == (page, ecc)
        value = previous[r['hw']]
        def config(before, seed):
            return ((before & ~(32767 << 16)) | (seed << 16)) if random else before
        for reg, seed in zip(o['regs'], (page, ecc)):
            assert reg['before'] == value
            value = config(value, seed)
            assert reg['configured'] == reg['off'] == value
            assert reg['on'] == value | ((1 << r['enableBit']) if random else 0)
        expected_rx = r['input'] if late_bug and r['bbm'] else r['marker']
        expected_tx = r['marker'] if late_bug and r['bbm'] else r['input']
        assert o['rx'] == expected_rx and o['tx'] == expected_tx
        if o['rx'] != r['marker'] or o['tx'] != r['input']:
            failures.append(r['id'])
        for name, seed, ecc_mode in (('read', page, False), ('write', ecc, True)):
            t = o[name]
            assert t['before'] == value and t['ecc'] == ecc_mode
            value = config(value, seed)
            assert t['during'] == value | ((1 << r['enableBit']) if random else 0)
            assert t['after'] == value and t['ret'] == r['fail']
            assert not (value & (1 << r['enableBit']))
        previous[r['hw']] = value
    assert bool(failures) == historical
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate-source', type=Path)
    parser.add_argument('--source-only', action='store_true')
    parser.add_argument('--case', default='')
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    if not __debug__:
        parser.error('assertions must remain enabled')
    pin = json.loads((HERE / 'source.json').read_text())
    source = (HERE / 'reference/sunxi_nand.c').read_text()
    assert hashlib.sha256(source.encode()).hexdigest() == pin['driver_sha256']
    historical = not args.candidate_source
    if args.candidate_source:
        source = args.candidate_source.read_text()
    register_contract(source)
    tables = seed_tables()
    records = inputs(tables)
    generated, hashes = fixture(source, fixed=not historical, extra_functions=(
        'sunxi_nfc_randomizer_read_buf', 'sunxi_nfc_randomizer_write_buf',
        'sunxi_nfc_randomize_buf'))
    generated.insert(2, (HERE / 'tests/randomizer_trace.h').read_text())
    definition = source.split('struct sunxi_nfc_oob_ops {', 1)[1].split('\n};', 1)[0]
    has_randomized = bool(re.search(r'bool\s+(?:randomized|use_randomizer);', definition))
    generated += [f'#define HAS_RANDOMIZED_FIELD {int(has_randomized)}',
                  header(records), '#line 1 "randomizer_trace.c"',
                  (HERE / 'tests/randomizer_trace.c').read_text()]
    names = defaultdict(list)
    for key in hashes:
        filename, name = key.split(':')
        names[filename].append(name)
    out = check('randomizer', {'check.c': '\n\n'.join(generated)}, hashes,
                expected='ALL RANDOMIZER TRACE CHECKS PASSED',
                coverage=({'sunxi_nand.c': source,
                           'toshiba-bbm-init.c': (HERE / 'reference/toshiba-bbm-init.c').read_text()}, names),
                require_all_called=False,
                artifacts=('randomizer-policy.jsonl', 'randomizer-tables.jsonl'), limits=[
                    'Actual source helpers with fixture MMIO/decoded USER_DATA; no board access.',
                    'Final ops selection is called explicitly; nand_scan/BBT ordering has separate tests.',
                    'No physical BCH decoding, transport, command reload, or concurrent I/O is simulated.',
                    'Synthetic geometries exercise helper contracts, not Toshiba part identification.'])
    source_report = json.loads((out / 'summary.json').read_text())
    coverage = source_report['coverage']['functions']
    # The shared extractor also includes a geometry helper used by other tests.
    assert all(v['calls'] for k, v in coverage.items()
               if k != 'sunxi_nand.c:sunxi_nfc_user_data_sz')
    raw = (out / 'randomizer-policy.jsonl').read_bytes()
    observed = [json.loads(line) for line in raw.splitlines()]
    assert len(observed) == len(records)
    for r, o in zip(records, observed):
        assert r['id'] == o['id']
        r['observed'] = o
    failures = check_observations(records, tables, historical=historical)
    rows = [json.loads(line) for line in (out / 'randomizer-tables.jsonl').read_bytes().splitlines()]
    assert len(rows) == 256
    for serial, row in enumerate(rows):
        step, index = (512 if serial < 128 else 1024), serial % 128
        assert (row['step'], row['index']) == (step, index)
        assert row['pageSeed'] == tables['PageSeeds'][index]
        assert row['eccSeed'] == tables[f'Ecc{step}'][index]
        assert row['bytes'] == [advance(row['eccSeed'], 8 * i) & 255 for i in range(4)]
    meta = dict(source_sha256=hashlib.sha256(source.encode()).hexdigest(),
                source_functions=hashes, records=len(records), historical=historical,
                historical_marker_failures=failures,
                observations_sha256=hashlib.sha256(raw).hexdigest(),
                table_observations_sha256=hashlib.sha256(
                    (out / 'randomizer-tables.jsonl').read_bytes()).hexdigest(),
                fixture_sha256=source_report['generated_sha256']['check.c'],
                bsp_sha256=hashlib.sha256((HERE / 'reference/compat/ndfc_base.c').read_bytes()).hexdigest())
    (out / 'inputs.json').write_text(json.dumps(records, indent=2) + '\n')
    (out / 'oracle-summary.json').write_text(json.dumps(dict(passed=True, **meta), indent=2) + '\n')
    print(f'Independent byte/register oracle: {len(records)} records, '
          f'{len(failures)} expected historical marker failures; logs: {out}', flush=True)
    if args.source_only:
        return 0
    tests = []
    for step in (512, 1024):
        phase = dict(Inputs=freeze([r for r in rows if r['step'] == step]), StepBytes=step)
        tests.append(case('NandRandomizerPhase', f'randomizer-phase-{step}', phase))
    for mutation in ('phase-minus-one', 'phase-plus-one'):
        test = case('NandRandomizerPhase', 'randomizer-' + mutation,
                    dict(Inputs=freeze(rows[:1]), StepBytes=512), 'SeedTable', 'mutation')
        test['constants']['Mutation'] = mutation
        tests.append(test)
    positive = [r for r in records if not (historical and r['mode'] == 2)]
    for i in range(0, len(positive), 64):
        tests.append(case('NandRandomizerPolicy', f'randomizer-policy-{i//64:02d}',
                          dict(tables, Records=model_records(positive[i:i + 64]))))
    tests += policy_controls(positive, tables)
    if historical:
        late = next(r for r in records if r['mode'] == 2 and r['bbm'])
        for invariant in ('Policy', 'ReadCompatibility', 'WriteCompatibility'):
            tests.append(case('NandRandomizerPolicy', 'randomizer-historical-' + invariant,
                              dict(tables, Records=model_records([late])), invariant, 'historical'))
    options = ['--case', args.case, '--timeout', str(args.timeout)]
    if args.download:
        options.append('--download')
    return tlc.main(options, cases=tests, modules={'NandRandomizerPhase': PHASE,
                                                  'NandRandomizerPolicy': POLICY}, metadata=meta)


if __name__ == '__main__':
    raise SystemExit(main())
