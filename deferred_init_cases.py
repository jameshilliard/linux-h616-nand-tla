# SPDX-License-Identifier: GPL-2.0-or-later
"""Finalize the ordinary-format policy before BBT access and publication."""


def extend_deferred_init(modules, add, mutation):
    module = 'NandDeferredInitialization'
    modules[module] = dict(
        invariants=['TypeOK', 'FinalBeforeBBT', 'OneBBT', 'PublishReady',
                    'NoLeak', 'SubpagePolicy'],
        actions=['Identify', 'Attach', 'Manufacturer', 'Tail', 'Finalize',
                 'BBT', 'Publish', 'Remove', 'Cleanup'])
    for early in (False, True):
        for late in (False, True):
            for vendor in (False, True):
                base = dict(EarlyScramble=early, LateScramble=late,
                            Vendor=vendor, SLC=not late)
                for fail in ('none', 'identify', 'attach', 'manufacturer',
                             'tail', 'bbt', 'bbt-after-alloc', 'publish'):
                    add(module, f'deferred-init-{int(early)}{int(late)}{int(vendor)}-{fail}',
                        dict(base, FailAt=fail))
    base = dict(EarlyScramble=False, LateScramble=True, Vendor=False,
                SLC=False, FailAt='none')
    mutation(module, base, 'scan-bbt-too-early', 'FinalBeforeBBT')
    mutation(module, base, 'omit-final-selection', 'FinalBeforeBBT')
    mutation(module, dict(base, Vendor=True), 'replace-vendor', 'FinalBeforeBBT')
    mutation(module, base, 'publish-before-bbt', 'PublishReady')
    mutation(module, base, 'duplicate-bbt', 'OneBBT')
    mutation(module, dict(base, FailAt='bbt-after-alloc'), 'leak-partial-bbt', 'NoLeak')
    # No current late-setting Toshiba/Hynix part is SLC. Finalizing OOB ops
    # after scan would NOT recompute the core's already-selected subpagesize.
    add(module, 'deferred-init-assumption-late-slc', dict(base, SLC=True),
        expected='SubpagePolicy', kind='assumption')
