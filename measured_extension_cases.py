# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded checks tied to the extended, opt-in hardware observations."""


def extend_measured(modules, add, mutation):
    modules.update({
        'NandMeasuredToggle': dict(invariants=['MeasuredContract', 'Bounds'],
                                   actions=['Transfer']),
        'NandResetObservation': dict(
            invariants=['ProbeRetainsBuffers', 'EvidenceNotOverstated', 'Bounds'],
            actions=['Reset', 'Observe', 'ReadNext', 'Finish']),
    })
    for read in (False, True):
        for page in (False, True):
            for chunk in (3, 4, 5):
                add('NandMeasuredToggle', f'toggle-{read}-{page}-{chunk}',
                    dict(Read=read, PageMode=page, Chunk=chunk))
    toggle = dict(Read=True, PageMode=True, Chunk=3)
    for change in ('advance-disabled', 'reset-on-enable', 'randomize-tail'):
        mutation('NandMeasuredToggle', toggle, change, 'MeasuredContract')
    for abort, good in ((False, True), (True, True), (True, False)):
        add('NandResetObservation', f'reset-observation-{abort}-{good}',
            dict(Abort=abort, FirstGood=good))
    reset = dict(Abort=True, FirstGood=False)
    mutation('NandResetObservation', reset, 'free-after-quiet', 'ProbeRetainsBuffers')
    mutation('NandResetObservation', reset, 'quiet-implies-fresh', 'EvidenceNotOverstated')
