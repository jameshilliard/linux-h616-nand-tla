# SPDX-License-Identifier: GPL-2.0-or-later
"""Finalize the ordinary-format policy before BBT access and publication."""


def extend_deferred_init(modules, add, mutation):
    module = 'NandDeferredInitialization'
    modules[module] = dict(
        invariants=['TypeOK', 'FinalBeforeBBT', 'OneBBT', 'PublishReady',
                    'NoLeak', 'SubpagePolicy', 'InstallOnce', 'StablePolicy',
                    'GenerationMatches', 'GeometryCompatible'],
        actions=['Identify', 'Attach', 'Manufacturer', 'Tail', 'Finalize',
                 'BBT', 'Publish', 'Remove', 'Cleanup'])
    for generation in ('legacy', 'h6'):
        generation_cases(module, generation, add)


def generation_cases(module, generation, add):
    def add_case(name, values, **kwargs):
        add(module, f'{generation}-{name}', dict(values, Generation=generation), **kwargs)

    def mutate(values, name, invariant):
        add_case(f'{module}-{name}', dict(values, Mutation=name),
                 expected=invariant, kind='mutation')

    for early in (False, True):
        for late in (False, True):
            for vendor in (False, True):
                base = dict(EarlyScramble=early, LateScramble=late,
                            Vendor=vendor, SLC=not late)
                for fail in ('none', 'identify', 'attach', 'manufacturer',
                             'tail', 'bbt', 'bbt-after-alloc', 'publish'):
                    add_case(f'deferred-init-{int(early)}{int(late)}{int(vendor)}-{fail}',
                        dict(base, FailAt=fail))
    base = dict(EarlyScramble=False, LateScramble=True, Vendor=False,
                SLC=False, FailAt='none')
    mutate(base, 'scan-bbt-too-early', 'FinalBeforeBBT')
    mutate(base, 'omit-final-selection', 'InstallOnce')
    mutate(dict(base, Vendor=True), 'replace-vendor', 'StablePolicy')
    mutate(base, 'publish-before-bbt', 'PublishReady')
    mutate(base, 'duplicate-bbt', 'OneBBT')
    mutate(dict(base, FailAt='bbt-after-alloc'), 'leak-partial-bbt', 'NoLeak')
    mutate(base, 'install-during-attach', 'InstallOnce')
    mutate(base, 'reinstall-before-bbt', 'InstallOnce')
    mutate(base, 'change-after-install', 'FinalBeforeBBT')
    mutate(base, 'wrong-geometry', 'GeometryCompatible')
    mutate(dict(base, Vendor=True), 'wrong-generation', 'GenerationMatches')
    add_case('deferred-init-wrong-ordinary-generation', dict(base, Mutation='wrong-generation'),
        expected='GenerationMatches', kind='mutation')
    # No current late-setting Toshiba/Hynix part is SLC. Finalizing OOB ops
    # after scan would NOT recompute the core's already-selected subpagesize.
    add_case('deferred-init-assumption-late-slc', dict(base, SLC=True),
        expected='SubpagePolicy', kind='assumption')
