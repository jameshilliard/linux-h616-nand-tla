# SPDX-License-Identifier: GPL-2.0-or-later
"""Private-buffer candidate policy and explicit weaker-hardware witnesses."""
def extend_private_recovery(modules, add, mutation):
    module = 'NandPrivateRecovery'
    modules[module] = dict(
        invariants=['NoFreedDMATarget', 'NoActiveReuse', 'SoundRead',
                    'ContextRestored', 'FailedRecoveryClosed', 'NoWriteReplay',
                    'ResetSerialized', 'Retention'],
        actions=['Launch', 'Timeout', 'SoftReset', 'AssertReset', 'DeassertReset',
                 'Return', 'Retry', 'Pio', 'NextRequest', 'Complete', 'Release'])
    base = dict(Direction='read', Fault='none', SoftRetires=False,
                BusRetires=True, BusClears=True, NextChip=1)
    for direction in ('read', 'write'):
        for fault in ('none', 'missing', 'assert', 'deassert'):
            for soft in (False, True):
                for chip in (1, 2):
                    add(module, f'private-{direction}-{fault}-{soft}-{chip}',
                        dict(base, Direction=direction, Fault=fault,
                             SoftRetires=soft, NextChip=chip))
    add(module, 'private-bus-does-not-retire', dict(base, BusRetires=False),
        expected='NoActiveReuse', kind='assumption')
    add(module, 'private-bus-does-not-clear-data', dict(base, BusClears=False),
        expected='SoundRead', kind='assumption')
    add(module, 'private-held-reset-does-not-retire',
        dict(base, Fault='deassert', BusRetires=False),
        expected='NoFreedDMATarget', kind='assumption')
    mutation(module, dict(base, Fault='assert'), 'free-unsafe', 'NoFreedDMATarget')
    mutation(module, dict(base, Fault='assert'), 'direct-caller-dma', 'NoFreedDMATarget')
    mutation(module, dict(base, Fault='deassert'), 'reopen', 'FailedRecoveryClosed')
    mutation(module, base, 'skip-context', 'ContextRestored')
    mutation(module, base, 'copy-timeout', 'SoundRead')
    mutation(module, dict(base, Direction='write'), 'replay-write', 'NoWriteReplay')
    # Alternative ownership policy, not a change to the submitted driver.
    # Direct caller DMA is safe in this abstraction only after retirement;
    # the reset API succeeding remains an explicit hardware assumption.
    for fault in ('none', 'deassert'):
        for soft in (False, True):
            for chip in (1, 2):
                add(module, f'direct-retired-read-{fault}-{soft}-{chip}',
                    dict(base, Fault=fault, SoftRetires=soft, NextChip=chip,
                         Mutation='direct-caller-dma'))
    add(module, 'direct-missing-reset-caller-lifetime',
        dict(base, Fault='missing', Mutation='direct-caller-dma'),
        expected='NoFreedDMATarget', kind='assumption')
