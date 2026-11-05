# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject missing, mismatched or unsuccessful reset-pulse observations."""
import json
import unittest

from hardware.reset_pulse_evidence import CAPTURE, FINAL_FIELDS, check


class ResetPulseEvidence(unittest.TestCase):
    def test_capture(self):
        check(json.loads(CAPTURE.read_text()))

    def test_mutations(self):
        def hide_active(c):
            for trial in c['native']['rows']:
                trial['abort']['regs'][2] &= ~24

        mutations = [
            lambda c: c.__setitem__('source_sha256', '0' * 64),
            lambda c: c.__setitem__('provider_source_sha256', '0' * 64),
            lambda c: c.__setitem__('data_policy', 'private'),
            lambda c: c.__setitem__('reset_api', 'split-calls'),
            lambda c: c.__setitem__('provider_delay_us', 2),
            lambda c: c['native']['rows'].pop(),
            lambda c: c['native']['rows'][0]['abort'].__setitem__('result', -5),
            lambda c: c['native']['rows'][0]['abort'].__setitem__('matches', False),
            lambda c: c['native']['rows'][0]['abort']['regs'].__setitem__(1, 1),
            lambda c: c['native']['rows'][0]['abort']['regs'].__setitem__(5, 0),
            lambda c: c['native']['rows'][0]['abort']['regs'].__setitem__(9, 0),
            lambda c: c['native']['rows'][0]['abort']['regs'].__setitem__(13, 0),
            lambda c: c['native']['rows'][0]['abort']['regs'].__setitem__(14, 0),
            lambda c: c['native']['rows'][0]['followup'].__setitem__('matches', False),
            hide_active,
        ]
        mutations.extend(lambda c, name=name: c['final_verification'].__setitem__(name, False)
                         for name in FINAL_FIELDS.split())
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                capture = json.loads(CAPTURE.read_text())
                mutate(capture)
                with self.assertRaises(AssertionError):
                    check(capture)
