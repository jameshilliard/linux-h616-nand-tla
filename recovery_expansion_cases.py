# SPDX-License-Identifier: GPL-2.0-or-later
"""Recovery composition, measured SRAM outcomes and write-side contracts."""


def extend_recovery_expansion(modules, add, mutation):
    module = 'NandRecoverySequence'
    modules[module] = dict(
        invariants=['MappingSafe', 'FreshCompletion', 'NoActiveReuse', 'SoundSuccess',
                    'ContextRestored', 'FailedRecoveryClosed',
                    'ResetSerialized', 'BoundedRequests'],
        actions=['Select', 'Map', 'Launch', 'Complete', 'Consume', 'Timeout',
                 'Mask', 'Drain', 'Stop', 'AssertReset', 'DeassertReset',
                 'Cleanup', 'RestoreClock', 'RestoreContext', 'Retry',
                 'Error', 'NextRequest', 'Quarantine'])
    base = dict(Backend='descriptor', Policy='bus-reset',
                Faults=('timeout', 'double-timeout', 'ok'),
                RecoveryFailure='none', FailureAt=1, StopRetires=True, BusRetires=True,
                RetireEvents=True, HiddenFault=True, BusClears=True, NextChip=1)
    for chip in (1, 2):
        for failure in ('none', 'assert', 'deassert', 'clock', 'context'):
            for at in (1, 2):
                add(module, f'recovery-sequence-chip{chip}-{failure}-{at}',
                    dict(base, NextChip=chip, RecoveryFailure=failure, FailureAt=at),
                    kind='candidate-policy')
    for backend in ('pio', 'external', 'register', 'descriptor'):
        add(module, f'recovery-source-normal-{backend}',
            dict(base, Backend=backend, Policy='source', Faults=('ok', 'ok')))
        add(module, f'recovery-source-quarantine-{backend}',
            dict(base, Backend=backend, Policy='quarantine', StopRetires=False))
    add(module, 'recovery-source-external-conditional-stop',
        dict(base, Backend='external', Policy='source', HiddenFault=False,
             Faults=('timeout', 'ok')))
    add(module, 'recovery-source-pio-timeout-reuse',
        dict(base, Backend='pio', Policy='source', HiddenFault=False, Faults=('timeout', 'ok')),
        expected='NoActiveReuse', kind='assumption')
    add(module, 'recovery-source-pio-retry-timeout-reuse',
        dict(base, Policy='source', HiddenFault=False, Faults=('double-timeout', 'ok')),
        expected='NoActiveReuse', kind='assumption')
    for backend in ('register', 'descriptor'):
        add(module, f'recovery-source-dirty-retry-{backend}',
            dict(base, Backend=backend, Policy='source', Faults=('timeout', 'ok')),
            expected='SoundSuccess', kind='hardware-contract')
    for backend in ('external', 'register', 'descriptor'):
        add(module, f'recovery-source-unretired-{backend}',
            dict(base, Backend=backend, Policy='source', StopRetires=False),
            expected='MappingSafe', kind='assumption')
    add(module, 'recovery-bus-reset-does-not-clear-data', dict(base, BusClears=False),
        expected='SoundSuccess', kind='assumption')
    add(module, 'recovery-old-event-survives', dict(base, RetireEvents=False),
        expected='FreshCompletion', kind='assumption')
    add(module, 'recovery-unproven-retirement-keeps-memory',
        dict(base, StopRetires=False, BusRetires=False))
    for backend in ('pio', 'external', 'register', 'descriptor'):
        for stop in (False, True):
            for bus in (False, True):
                add(module, f'recovery-independent-retirement-{backend}-{stop}-{bus}',
                    dict(base, Backend=backend, StopRetires=stop, BusRetires=bus),
                    kind='candidate-policy')
    mutation(module, base, 'skip-context', 'ContextRestored')
    mutation(module, dict(base, RecoveryFailure='deassert'),
             'reopen-failed-reset', 'FailedRecoveryClosed')

    module = 'NandSramPlacement'
    modules[module] = dict(
        invariants=['SoundCopy', 'SecondBankPreserved', 'CleanPrefix',
                    'FailedReadNotPublished', 'SecondReadFresh'],
        actions=['Recover', 'Position', 'Transfer', 'Complete', 'Copy', 'NextRequest'])
    base = dict(Count=4, Offset=1, Extra=2, Partial=False, Fault='none',
                Policy='source', RecoveryWorks=True)
    for count in (1, 3, 4, 7, 8):
        for partial in (False, True):
            add(module, f'sram-clean-{count}-{partial}', dict(base, Count=count, Partial=partial))
    for fault in ('rotate', 'spill'):
        for policy in ('source', 'clear-sram', 'bus-reset', 'discard', 'reject'):
            bad = policy in ('source', 'clear-sram')
            add(module, f'sram-dirty-{fault}-{policy}', dict(base, Fault=fault, Policy=policy),
                expected='SoundCopy' if bad else None,
                kind='hardware-contract' if bad else 'candidate-policy')
        add(module, f'sram-dirty-error-{fault}', dict(base, Fault=fault, Partial=True))
    add(module, 'sram-unproven-discard', dict(base, Fault='spill', Policy='discard', RecoveryWorks=False),
        expected='SoundCopy', kind='assumption')
    mutation(module, dict(base, Partial=True), 'copy-failed-prefix', 'FailedReadNotPublished')

    module = 'NandProgramRecovery'
    modules[module] = dict(
        invariants=['MappingSafe', 'NoWriteReplay', 'ConfirmAfterData',
                    'SuccessDurable', 'NoProgramBeforeConfirm',
                    'NeighborPreserved', 'ReadObservesMedia', 'NoRollbackClaim'],
        actions=['Prepare', 'FirstStep', 'SecondStep', 'Retire', 'TransferTail',
                 'Confirm', 'Program', 'Status', 'Abort', 'Cleanup',
                 'Error', 'ResetChip', 'Read'])
    base = dict(DMA=True, Fault='none', StopRetires=True, ResetOK=True, NeighborStable=True)
    faults = ('none', 'prepare', 'first', 'second', 'tail', 'confirm-rejected',
              'confirm-accepted', 'busy-timeout', 'status-fail', 'power-loss')
    for dma in (False, True):
        for fault in faults:
            for reset in (False, True):
                add(module, f'program-recovery-{dma}-{fault}-{reset}',
                    dict(base, DMA=dma, Fault=fault, ResetOK=reset))
    add(module, 'program-unretired-dma', dict(base, Fault='first', StopRetires=False),
        expected='MappingSafe', kind='assumption')
    add(module, 'program-power-loss-neighbor-contract',
        dict(base, Fault='power-loss', NeighborStable=False),
        expected='NeighborPreserved', kind='assumption')
    mutation(module, dict(base, Fault='second'), 'replay-partial-write', 'NoWriteReplay')
    mutation(module, dict(base, Fault='confirm-accepted'), 'timeout-is-success', 'SuccessDurable')
    mutation(module, base, 'confirm-before-tail', 'ConfirmAfterData')
    mutation(module, dict(base, Fault='busy-timeout'), 'reset-rolls-back', 'NoRollbackClaim')

    module = 'NandMeasuredPage'
    modules[module] = dict(
        invariants=['MeasuredContract', 'StoredFormat', 'RoundTrip',
                    'FailedReadNotCopied', 'ContextFresh', 'NoPartialConfirm',
                    'ErrorLength', 'Bounds'],
        actions=['StageProtected', 'WriteTail', 'Confirm', 'ReadProtected',
                 'ReadTail', 'Finish', 'Reuse'])
    base = dict(Size=20, WriteChunk=8, ReadChunk=8, FormatChunk=8,
                PageMode=False, SoftwareRead=False, SoftwareChunk=0, Fault='none')
    for size in (0, 4, 8, 12, 20):
        for fault in ('none', 'write-tail', 'read-tail', 'confirm-timeout'):
            add(module, f'measured-page-{size}-{fault}', dict(base, Size=size, Fault=fault))
    for size in (1, 3, 9, 19):
        add(module, f'measured-page-nonword-{size}', dict(base, Size=size),
            expected='RoundTrip', kind='hardware-contract')
    add(module, 'measured-page-continuous-media-format', dict(base, FormatChunk=20),
        expected='StoredFormat', kind='format-contract')
    add(module, 'measured-page-mode-asymmetric', dict(base, PageMode=True),
        expected='RoundTrip', kind='hardware-contract')
    add(module, 'measured-page-read-rechunked', dict(base, ReadChunk=4),
        expected='RoundTrip', kind='format-contract')
    for size in (0, 3, 4, 8, 9, 12, 20):
        add(module, f'measured-page-vendor-software-{size}', dict(base, SoftwareRead=True, Size=size),
            expected='RoundTrip' if size>8 else None,
            kind='source-contract' if size>8 else 'positive')
    # Decode relative to the start of the written tail, not raw-read chunks
    # (which can also contain parity). Keep the old failure cases above.
    fixed = dict(base, SoftwareRead=True, SoftwareChunk=8)
    for size in (0, 1, 3, 7, 8, 9, 12, 16, 19, 20):
        for read in (3, 8, 11):
            for fault in ('none', 'read-tail', 'write-tail'):
                add(module, f'measured-page-fixed-software-{size}-{read}-{fault}',
                    dict(fixed, Size=size, ReadChunk=read, Fault=fault))
    mutation(module, fixed, 'decode-continuously', 'RoundTrip')
    mutation(module, fixed, 'decode-from-raw-column', 'RoundTrip')
    add(module, 'measured-page-wrong-software-chunk', dict(fixed, SoftwareChunk=4),
        expected='RoundTrip', kind='mutation')
    mutation(module, base, 'writer-continuous', 'MeasuredContract')
    mutation(module, dict(base, Size=3), 'xor-read-tail', 'MeasuredContract')
    mutation(module, dict(base, Fault='read-tail'), 'copy-failed-tail', 'FailedReadNotCopied')
    mutation(module, base, 'reuse-old-context', 'ContextFresh')
