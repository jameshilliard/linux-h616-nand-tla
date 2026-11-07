# SPDX-License-Identifier: GPL-2.0-or-later
"""Do not confuse TLC infrastructure failures with expected counterexamples."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run import invariant_counterexample, temporal_counterexample


class CounterexampleResult(unittest.TestCase):
    def test_temporal_named(self):
        log = ("Temporal property Terminates was violated.\n"
               "The following behavior constitutes a counter-example:")
        self.assertTrue(temporal_counterexample(13, log, "Terminates"))
        self.assertFalse(temporal_counterexample(13, log, "Other"))
        for rc in (-1, 0, 1, 12):
            self.assertFalse(temporal_counterexample(rc, log, "Terminates"))

    def test_temporal_legacy_single_property(self):
        log = ("Temporal properties were violated.\n"
               "The following behavior constitutes a counter-example:")
        self.assertFalse(temporal_counterexample(13, log, "Terminates"))
        self.assertTrue(temporal_counterexample(13, log, "Terminates", unnamed=True))
        self.assertFalse(temporal_counterexample(13, log.split("\n")[0],
                                                "Terminates", unnamed=True))
        for rc in (-1, 0, 1, 12):
            self.assertFalse(temporal_counterexample(rc, log, "Terminates", unnamed=True))

    def test_transition(self):
        log = 'Error: Invariant ExactState is violated.\nThe behavior up to this point is:'
        self.assertTrue(invariant_counterexample(12, log, 'ExactState'))
        self.assertFalse(invariant_counterexample(12, log, 'ByteOrder'))
        self.assertFalse(invariant_counterexample(-1, log, 'ExactState'))

    def test_initial(self):
        log = ('Error: Invariant ExactState is violated by the initial state:\n'
               's = [word |-> 32768]\n"CounterExample written: trace"')
        self.assertTrue(invariant_counterexample(12, log, 'ExactState'))
        self.assertFalse(invariant_counterexample(12, log, 'Exact'))
        self.assertFalse(invariant_counterexample(0, log, 'ExactState'))
        self.assertFalse(invariant_counterexample(12, log.split('"')[0], 'ExactState'))

    def test_infrastructure(self):
        for rc in (-1, 0, 12, 13):
            for log in ('HARNESS TIMEOUT', 'Error: Parsing or semantic analysis failed.',
                        'Error: TLC threw an unexpected exception.', ''):
                self.assertFalse(invariant_counterexample(rc, log, 'ExactState'))


if __name__ == '__main__':
    unittest.main()
