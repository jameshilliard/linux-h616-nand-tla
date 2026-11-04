# SPDX-License-Identifier: GPL-2.0-or-later
"""Measured failure boundary, with explicitly conditional recovery policies."""


def extend_post_abort(modules, add, mutation):
    module = 'NandPostAbortRequest'
    modules[module] = dict(
        invariants=['Geometry', 'MappingSafe', 'ProbeOwnership',
                    'CompleteBeforeCopy', 'Accounted', 'SoundSuccess',
                    'SecondRequestFresh'],
        actions=['Reset', 'Observe', 'Recover', 'Map', 'Position', 'Receive',
                 'CommandDone', 'DmaDone', 'Capture', 'Copy', 'Advance',
                 'Return', 'NextRequest'])
    base = dict(DMA=True, Abort=True, Fault='rotate', Policy='none',
                StopRetires=True, DrainClears=True, BusClears=True,
                CompletionClears=True)
    for dma in (False, True):
        for abort in (False, True):
            for fault in ('none', 'rotate', 'decode'):
                for policy in ('none', 'clear-sram', 'nand-reset', 'drain',
                               'bus-reset', 'quarantine'):
                    bad = abort and fault == 'rotate' and policy in (
                        'none', 'clear-sram', 'nand-reset')
                    add(module, f'post-abort-{dma}-{abort}-{fault}-{policy}',
                        dict(base, DMA=dma, Abort=abort, Fault=fault, Policy=policy),
                        expected='SoundSuccess' if bad else None,
                        kind='measured-boundary' if bad else 'positive')
    # Clearing after a throwaway read and bus reset is NOT assumed universal.
    for policy, contract in (('drain', 'DrainClears'), ('bus-reset', 'BusClears')):
        add(module, f'post-abort-unproven-{policy}',
            dict(base, Policy=policy, **{contract: False}),
            expected='SoundSuccess', kind='assumption')
    # Preserve ownership even when quiet observations cannot prove retirement.
    add(module, 'post-abort-retain-on-unproven-stop',
        dict(base, Policy='quarantine', StopRetires=False))
    mutation(module, dict(base, StopRetires=False), 'free-after-quiet', 'MappingSafe')
    add(module, 'post-abort-nonclearing-command',
        dict(base, CompletionClears=False), expected='SoundSuccess', kind='assumption')
