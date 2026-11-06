# SPDX-License-Identifier: GPL-2.0-or-later
"""Marker policy must follow scrambling enabled after controller attach."""


def extend_bbm_init(modules, add, mutation):
    modules['NandBbmInitialization'] = dict(
        invariants=['TypeOK', 'ReadCompatibility', 'WriteCompatibility'],
        actions=['Identify', 'Attach', 'Manufacturer', 'Read', 'Write'])
    for early in (False, True):
        for late in (False, True):
            for vendor in (False, True):
                for marker in (0, 1):
                    add('NandBbmInitialization',
                        f'bbm-init-early{int(early)}-late{int(late)}-vendor{int(vendor)}-marker{marker}',
                        dict(EarlyScramble=early, LateScramble=late,
                             Vendor=vendor, Marker=marker))
    base = dict(EarlyScramble=False, LateScramble=True, Vendor=False, Marker=1)
    mutation('NandBbmInitialization', base, 'cached-attach', 'ReadCompatibility')
    # Independently exercise the write invariant: the read counterexample must
    # not prevent exploration through the program step.
    add('NandBbmInitialization', 'bbm-init-cached-attach-write',
        dict(base, Mutation='cached-attach'), expected='WriteCompatibility', kind='mutation')
    mutation('NandBbmInitialization', dict(base, LateScramble=False),
             'unconditional-transform', 'ReadCompatibility')
    mutation('NandBbmInitialization', dict(base, Vendor=True),
             'transform-vendor', 'ReadCompatibility')
