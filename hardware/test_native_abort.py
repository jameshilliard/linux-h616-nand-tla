# SPDX-License-Identifier: GPL-2.0-or-later
"""Mutation controls for the actual-driver recovery capture, without hardware."""
import json
import unittest

from hardware.replay_native_abort import CAPTURE, check


class NativeAbortEvidence(unittest.TestCase):
    def test_capture(self):
        check(json.loads(CAPTURE.read_text()))

    def test_mutations(self):
        def observation(c):
            return c['native_abort']['rows'][0]['observations'][0]

        mutations = (
            lambda c: observation(c).__setitem__('result', -5),
            lambda c: observation(c).__setitem__('matches', False),
            lambda c: observation(c).__setitem__('sha256', '0' * 64),
            lambda c: observation(c)['regs'].__setitem__(0, 1),
            lambda c: observation(c)['regs'].__setitem__(1, 1),
            lambda c: observation(c)['regs'].__setitem__(10, 0),
            lambda c: observation(c)['regs'].__setitem__(14, 0),
            lambda c: c['native_abort']['rows'].__setitem__(1, c['native_abort']['rows'][0]),
            lambda c: c['native_abort'].__setitem__('restored', False),
            lambda c: c['stress']['rows'][0].__setitem__('bad_data', 1),
            lambda c: c['stress']['rows'][0].__setitem__('fallback', 1),
            lambda c: c['final_verification'].__setitem__('all_spl_restored', False),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                capture = json.loads(CAPTURE.read_text())
                mutate(capture)
                with self.assertRaises(AssertionError):
                    check(capture)
