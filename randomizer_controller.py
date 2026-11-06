# SPDX-License-Identifier: GPL-2.0-or-later
"""Bit-array peripheral model and explicit evidence/uncertainty profiles.

No kernel helpers or previous measured_stream implementation are used here.
Unobserved side effects are choices, not default claims about real silicon.
"""
from copy import deepcopy

RESET_CONFIG = 0x4a800008
UNCERTAIN = {'keep', 'invalidate', 'reload'}


def profile(name):
    """Sets denote alternatives that formal checking must retain."""
    if name not in ('h616', 'a10', 'a20', 'a23', 'a33'):
        raise ValueError('unknown evidence profile: ' + name)
    legacy = name != 'h616'
    result = dict(enableBit=9 if legacy else 5,
                  directionBit=10 if legacy else 6,
                  pageBit=11 if legacy else 7,
                  seedWrite=set(UNCERTAIN), modeWrite=set(UNCERTAIN),
                  sameWrite={'keep'} if not legacy else {'keep', 'reload'},
                  enableWrite={'keep'} if not legacy else set(UNCERTAIN),
                  select={'keep', 'invalidate'}, nandReset={'keep', 'invalidate'},
                  controllerReset={'keep', 'invalidate', 'zero'},
                  abort={'keep', 'invalidate', 'zero'},
                  normalBlockRead={True}, normalBlockWrite={True},
                  normalPageRead={False}, normalPageWrite={True},
                  eccStart={True}, blockBoundary={True},
                  pageBoundary={False, True},
                  readTailBypass={True})
    if legacy:
        for field in ('normalBlockRead', 'normalBlockWrite', 'normalPageRead',
                      'normalPageWrite', 'readTailBypass', 'eccStart', 'blockBoundary'):
            result[field] = {False, True}
    return result


def bsp_contract_profile(name):
    """Expected per-codeword BSP contract, not a measurement of older silicon."""
    result = profile(name)
    result.update(eccStart={True}, blockBoundary={True})
    return result


def concrete(base, **choices):
    """Resolve every unknown explicitly; never choose an arbitrary set element."""
    result = deepcopy(base)
    for name, value in result.items():
        if isinstance(value, set):
            if len(value) == 1:
                result[name] = next(iter(value))
            elif name in choices and choices[name] in value:
                result[name] = choices[name]
            else:
                raise ValueError('unresolved hardware assumption: ' + name)
    return result


def measured_profile():
    # These choices resolve unmeasured transitions only for deterministic replay.
    # Recorded traces start with a fresh command context; they do not validate
    # seed/mode/reset changes, which the TLA+ envelope keeps nondeterministic.
    return concrete(profile('h616'), seedWrite='invalidate', modeWrite='invalidate',
                    select='invalidate', nandReset='invalidate',
                    controllerReset='invalidate', abort='invalidate', pageBoundary=True)


def config(seed, *, legacy=False, enabled=True, page=False, msb=False):
    return ((seed & 0x7fff) << 16 | int(enabled) << (9 if legacy else 5) |
            int(msb) << (10 if legacy else 6) | int(page) << (11 if legacy else 7))


def command(data, *, expected=None, read=True, kind='NORMAL', boundaries=(),
            column=0, aborts=()):
    return dict(op='command', input=tuple(data), expected=tuple(expected or ()),
                check=expected is not None, read=read, kind=kind,
                boundaries=set(boundaries), column=column, aborts=set(aborts))


class Controller:
    """Exact bytes for one resolved profile; no MMIO, DMA or flash side effects."""
    def __init__(self, policy, initial=0, valid=False):
        if any(isinstance(v, set) for v in policy.values()):
            raise ValueError('resolve every hardware alternative before byte replay')
        self.policy = policy
        self.cfg = RESET_CONFIG
        self.bits = [(initial >> i) & 1 for i in range(15)]
        self.valid = valid
        self.chip = 0

    @property
    def enabled(self):
        return bool(self.cfg & 1 << self.policy['enableBit'])

    @property
    def page(self):
        return bool(self.cfg & 1 << self.policy['pageBit'])

    def shift(self):
        self.bits = self.bits[1:] + [self.bits[0] ^ self.bits[1]]

    def load(self):
        self.bits = [(self.cfg >> (16 + i)) & 1 for i in range(15)]
        for _ in range(15):
            self.shift()
        self.valid = True

    def effect(self, value):
        if value == 'reload':
            self.load()
        elif value == 'invalidate':
            self.valid = False
        elif value == 'zero':
            self.bits = [0] * 15
        elif value != 'keep':
            raise ValueError(value)

    def write(self, cfg):
        old = self.cfg
        if (cfg >> 16) & 0x7fff != (old >> 16) & 0x7fff:
            field = 'seedWrite'
        elif (cfg ^ old) & ((1 << self.policy['pageBit']) |
                            (1 << self.policy['directionBit'])):
            field = 'modeWrite'
        elif not self.enabled and cfg & 1 << self.policy['enableBit']:
            field = 'enableWrite'
        else:
            field = 'sameWrite'
        self.cfg = cfg
        self.effect(self.policy[field])

    def reset(self, kind, value=0):
        if kind == 'select':
            self.chip = value
            field = 'select'
        elif kind == 'nand-reset':
            field = 'nandReset'
        elif kind in ('bus-reset', 'soft-reset'):
            self.cfg = RESET_CONFIG
            field = 'controllerReset'
        else:
            raise ValueError(kind)
        self.effect(self.policy[field])

    def transfer(self, event, abort=None):
        read, kind = event['read'], event['kind']
        name = 'normal' + ('Page' if self.page else 'Block') + ('Read' if read else 'Write')
        restart = self.policy[name] if kind == 'NORMAL' else self.policy['eccStart']
        if self.enabled and (restart or not self.valid):
            self.load()
        size = len(event['input'])
        limit = size if abort is None else abort
        assert 0 <= limit <= size
        result = bytearray()
        for offset, byte in enumerate(event['input'][:limit]):
            if (kind != 'NORMAL' and offset in event['boundaries'] and self.enabled
                    and self.policy['pageBoundary' if self.page else 'blockBoundary']):
                self.load()
            mask = 0
            if self.enabled:
                for bit in range(8):
                    slot = 7 - bit if self.cfg & 1 << self.policy['directionBit'] else bit
                    mask |= self.bits[0] << slot
                    self.shift()
                if (kind == 'NORMAL' and read and size % 4 and offset == size - 1
                        and self.policy['readTailBypass']):
                    mask = 0
            result.append(byte ^ mask)
        if abort is not None:
            self.effect(self.policy['abort'])
        if event['check']:
            assert tuple(result) == event['expected'][:limit], 'physical stream mismatch'
        return bytes(result)

    def run(self, events):
        output = []
        for event in events:
            if event['op'] == 'config':
                self.write(event['value'])
            elif event['op'] == 'command':
                output.append(self.transfer(event))
            else:
                self.reset(event['op'], event.get('value', 0))
        return output
