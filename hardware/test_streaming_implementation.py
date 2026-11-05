# SPDX-License-Identifier: GPL-2.0-or-later
"""Make the final-implementation evidence checker reject incomplete evidence."""
import json
import unittest

from hardware.streaming_implementation import CAPTURE, check


class StreamingImplementationEvidence(unittest.TestCase):
    def test_capture(self):
        check(json.loads(CAPTURE.read_text()))

    def test_mutations(self):
        def abort(c):
            return c['native']['rows'][0]['abort']

        def hide_active(c):
            for row in c['native']['rows']:
                row['abort']['regs'][2] &= ~24

        mutations = (
            lambda c: c.__setitem__('source_sha256', '0' * 64),
            lambda c: c.__setitem__('data_policy', 'private'),
            lambda c: abort(c).__setitem__('result', -5),
            lambda c: abort(c).__setitem__('matches', False),
            lambda c: abort(c)['regs'].__setitem__(1, 1),
            lambda c: abort(c)['regs'].__setitem__(5, 0),
            lambda c: abort(c)['regs'].__setitem__(9, 0),
            lambda c: abort(c)['regs'].__setitem__(13, 0),
            lambda c: abort(c)['regs'].__setitem__(14, 0),
            lambda c: c['native']['rows'].pop(),
            lambda c: c['native']['rows'][0]['followup'].__setitem__('matches', False),
            hide_active,
            lambda c: c['stress']['rows'][0]['regs'].__setitem__(1, 1),
            lambda c: c['stress']['rows'][0].__setitem__('matches', False),
            lambda c: c['stress']['rows'].pop(),
            lambda c: c['final_verification'].__setitem__('all_spl_unchanged', False),
            lambda c: c['final_verification'].__setitem__('ubi_block_unchanged', False),
            lambda c: c['final_verification'].__setitem__('allow_write_disabled', False),
            lambda c: c['final_verification'].__setitem__('normal_boot_restored', False),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                capture = json.loads(CAPTURE.read_text())
                mutate(capture)
                with self.assertRaises(AssertionError):
                    check(capture)
