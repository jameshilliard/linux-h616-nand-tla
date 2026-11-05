# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline negative controls for the streaming-DMA observation checker."""
import json
import unittest

from hardware.replay_direct import CAPTURE, check


class DirectEvidence(unittest.TestCase):
    def test_capture(self):
        check(json.loads(CAPTURE.read_text()))

    def test_mutations(self):
        def raw(c):
            return c['raw']['rows'][16]

        def native(c):
            return c['native']['rows'][0]['abort']

        def hide_active(c):
            for row in c['raw']['rows']:
                row['regs'][0] &= ~24

        mutations = (
            lambda c: raw(c).__setitem__('result', -5),
            lambda c: raw(c)['regs'].__setitem__(6, 0),
            lambda c: raw(c)['regs'].__setitem__(11, 0),
            lambda c: raw(c)['regs'].__setitem__(12, 0),
            lambda c: raw(c).__setitem__('next_matches', False),
            lambda c: c['raw']['rows'][-1]['regs'].__setitem__(8, 1),
            lambda c: c['raw']['rows'].__setitem__(1, c['raw']['rows'][0]),
            lambda c: c['raw'].__setitem__('unchanged', False),
            hide_active,
            lambda c: native(c)['regs'].__setitem__(1, 1),
            lambda c: native(c)['regs'].__setitem__(5, 0),
            lambda c: native(c)['regs'].__setitem__(10, 0),
            lambda c: native(c)['regs'].__setitem__(13, 0),
            lambda c: native(c)['regs'].__setitem__(9, 0),
            lambda c: native(c).__setitem__('matches', False),
            lambda c: c['native']['rows'].__setitem__(1, c['native']['rows'][0]),
            lambda c: c['native'].__setitem__('path_specific_oob_verified', False),
            lambda c: c['ecc_stress']['rows'][0]['regs'].__setitem__(0, 1),
            lambda c: c['final_verification'].__setitem__('ubi_block_unchanged', False),
            lambda c: c['final_verification'].__setitem__('normal_boot_restored', False),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                capture = json.loads(CAPTURE.read_text())
                mutate(capture)
                with self.assertRaises(AssertionError):
                    check(capture)
