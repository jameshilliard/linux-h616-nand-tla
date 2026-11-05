# SPDX-License-Identifier: GPL-2.0-or-later
"""Caller DMA ownership, mandatory reset, safe failure and availability limits."""
def extend_streaming_recovery(modules, add, mutation):
    module = 'NandStreamingRecovery'
    modules[module] = dict(
        invariants=['CallerSafe', 'MappingSafe', 'ResetSerialized', 'NoMMIOHeld',
                    'SoundRead', 'NoWriteReplay', 'VerifiedAdmission'],
        actions=['Probe', 'Map', 'Launch', 'Complete', 'Quiesce', 'SoftReset',
                 'AssertReset', 'RetryReset', 'DeassertReset', 'Cleanup', 'Retry',
                 'Pio', 'Return', 'NextRequest'])
    base = dict(Direction='read', Fault='none', Normal=False, Launched=True,
                BusRetires=True, BusClears=True)
    for direction in ('read', 'write'):
        for fault in ('none', 'probe-missing', 'probe-assert', 'map',
                      'assert-transient', 'assert-permanent', 'deassert'):
            add(module, f'streaming-{direction}-{fault}',
                dict(base, Direction=direction, Fault=fault))
        add(module, f'streaming-{direction}-normal', dict(base, Direction=direction, Normal=True))
    add(module, 'streaming-write-begin-failure', dict(base, Direction='write', Launched=False))
    add(module, 'streaming-permanent-failure-stalls', dict(base, Fault='assert-permanent'),
        expected='RequestReturns', temporal=True, kind='assumption')
    add(module, 'streaming-bus-does-not-retire', dict(base, BusRetires=False),
        expected='MappingSafe', kind='assumption')
    add(module, 'streaming-held-reset-does-not-retire', dict(base, BusRetires=False, Fault='deassert'),
        expected='MappingSafe', kind='assumption')
    add(module, 'streaming-bus-does-not-clear', dict(base, BusClears=False),
        expected='SoundRead', kind='assumption')
    mutation(module, base, 'unmap-before-reset', 'MappingSafe')
    mutation(module, dict(base, Fault='assert-permanent'), 'return-on-assert-failure', 'MappingSafe')
    mutation(module, dict(base, Fault='deassert'), 'cleanup-mmio-held', 'NoMMIOHeld')
    mutation(module, dict(base, Fault='deassert'), 'reopen', 'NoMMIOHeld')
    mutation(module, dict(base, Fault='probe-missing'), 'admit-without-reset', 'VerifiedAdmission')
    mutation(module, dict(base, Direction='write'), 'replay-write', 'NoWriteReplay')
    mutation(module, base, 'skip-context', 'SoundRead')
