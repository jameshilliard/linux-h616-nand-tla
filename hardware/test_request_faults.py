# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject incomplete/incorrect evidence, including absent fault reachability."""
import json
import unittest

from .request_faults_evidence import CAPTURE, check


class RequestFaultEvidence(unittest.TestCase):
    def test_capture(self):
        check(json.loads(CAPTURE.read_text()))

    def test_mutations(self):
        def writes(c):
            return c['observations']['write']

        def pio(c):
            return c['observations']['pio']

        def clear_all(c, rows, index, mask):
            for row in rows(c):
                row['regs'][index] &= ~mask

        mutations = (
            lambda c: c.__setitem__('source_sha256', '0' * 64),
            lambda c: writes(c).pop(),
            lambda c: writes(c)[0].__setitem__('result', 0),
            lambda c: writes(c)[0]['regs'].__setitem__(3, 1),
            lambda c: writes(c)[0]['regs'].__setitem__(4, 0),
            lambda c: writes(c)[0]['regs'].__setitem__(5, 1),
            lambda c: writes(c)[0]['regs'].__setitem__(8, 1),
            lambda c: writes(c)[0]['regs'].__setitem__(13, 0),
            lambda c: writes(c)[0]['regs'].__setitem__(14, 0),
            lambda c: writes(c)[0].__setitem__('unchanged', False),
            lambda c: writes(c)[0].__setitem__('raw_matches', False),
            lambda c: clear_all(c, writes, 0, 24),
            lambda c: pio(c).pop(),
            lambda c: pio(c)[0].__setitem__('matches', False),
            lambda c: pio(c)[0]['regs'].__setitem__(9, 0),
            lambda c: pio(c)[0]['regs'].__setitem__(8, 1),
            lambda c: pio(c)[0]['regs'].__setitem__(6, 1),
            lambda c: pio(c)[0]['regs'].__setitem__(10, 0),
            lambda c: clear_all(c, pio, 4, 24),
            lambda c: clear_all(c, pio, 5, 2),
            lambda c: c['observations'].__setitem__('restored', False),
            lambda c: c['final_verification'].__setitem__('ordinary_kernel_has_no_test_hooks', False),
            lambda c: c['final_verification'].__setitem__('ordinary_ecc_failed_delta', 1),
            lambda c: c['final_verification'].__setitem__('normal_boot_restored', False),
        )
        for i, mutate in enumerate(mutations):
            with self.subTest(mutation=i):
                capture = json.loads(CAPTURE.read_text())
                mutate(capture)
                with self.assertRaises(AssertionError):
                    check(capture)


if __name__ == '__main__':
    unittest.main()
