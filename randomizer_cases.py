# SPDX-License-Identifier: GPL-2.0-or-later
"""Bit-accurate arithmetic and source-observation checks; no physical I/O."""
from copy import deepcopy

MATH = ['NandRandomizerMath']
BITS = dict(invariants=['ExactState', 'ByteOrder', 'Bounds'],
            actions=['Shift'], dependencies=MATH)
PHASE = dict(invariants=['SeedTable', 'ByteStream', 'Bounds'],
             actions=['Byte'], dependencies=MATH)
POLICY = dict(invariants=['Policy', 'Seeds', 'Registers', 'ReadCompatibility',
                          'WriteCompatibility', 'ReturnedClean', 'Transport'],
              actions=['Select', 'Configure', 'Read', 'Write', 'Cleanup', 'NextRecord'],
              dependencies=MATH)


def extend_randomizer(modules, add, mutation):
    module = 'NandRandomizerBits'
    modules[module] = BITS
    add(module, 'randomizer-bits-all-u16', dict(SeedLo=0, SeedHi=65535, Count=1))
    for lo, hi in ((0, 7), (32760, 32775), (65528, 65535)):
        add(module, f'randomizer-bits-{lo}-{hi}', dict(SeedLo=lo, SeedHi=hi, Count=24))
    base = dict(SeedLo=0, SeedHi=7, Count=16)
    mutation(module, base, 'wrong-tap', 'ExactState')
    mutation(module, dict(base, SeedLo=32768, SeedHi=32775), 'omit-mask', 'ExactState')
    mutation(module, base, 'reverse-byte', 'ByteOrder')


def case(module, name, constants, expected=None, kind='positive'):
    return dict(module=module, name=name, constants=dict(constants, Mutation='none'),
                expected=expected, kind=kind)


def policy_controls(records, tables):
    """Alter captured observations, not kernel code. Require specific failures."""
    controls = []
    selected = next(r for r in records if r['mode'] == 1 and r['bbm'] and r['fail'])
    for name, invariant in (
        ('stale-policy', 'Policy'), ('wrong-seed', 'Seeds'),
        ('wrong-enable-bit', 'Registers'), ('clobber-control', 'Registers'),
        ('bad-read-marker', 'ReadCompatibility'), ('bad-write-marker', 'WriteCompatibility'),
        ('enabled-after-error', 'ReturnedClean'), ('swallow-error', 'Transport')):
        r = deepcopy(selected)
        o = r['observed']
        if name == 'stale-policy':
            o['compensate'] = False
        elif name == 'wrong-seed':
            o['eccSeed'] ^= 1
        elif name == 'wrong-enable-bit':
            o['regs'][0]['on'] ^= 1 << r['enableBit']
        elif name == 'clobber-control':
            o['regs'][0]['configured'] ^= 2
        elif name == 'bad-read-marker':
            o['rx'][0] ^= 1
        elif name == 'bad-write-marker':
            o['tx'][1] ^= 1
        elif name == 'enabled-after-error':
            o['read']['after'] |= 1 << r['enableBit']
        else:
            o['read']['ret'] = 0 if r['fail'] else -110
        controls.append(case('NandRandomizerPolicy', 'randomizer-control-' + name,
                             dict(tables, Records=model_records([r])), invariant, 'mutation'))
    return controls


def freeze(value):
    """Use tuples for sequences rendered into TLA+ constants."""
    if isinstance(value, list):
        return tuple(map(freeze, value))
    if isinstance(value, dict):
        return {k: freeze(v) for k, v in value.items()}
    return value


def model_records(records):
    """Retain bit 31 separately: TLC integers are signed 32-bit values."""
    result = deepcopy(records)
    for r in result:
        o = r['observed']
        groups = [(reg, ('before', 'configured', 'on', 'off')) for reg in o['regs']]
        groups += [(o[name], ('before', 'during', 'after')) for name in ('read', 'write')]
        for group, fields in groups:
            for key in fields:
                value = group[key]
                group[key] = dict(lo=value & 0x7fffffff, hi=bool(value >> 31))
    return freeze(result)
