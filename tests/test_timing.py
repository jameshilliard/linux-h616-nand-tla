# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the source adapter and negative controls independently of TLC."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from boundary_common import HERE, sources
from run_timing import minimum, model_cases, oracle, records, source_fixture, validate_observations
from timing_cases import PROFILES


class TimingChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = records()
        cls.source = sources()

    def test_setup_cycles(self):
        d = [0] * 21
        d[0] = 17
        self.assertEqual(minimum(d, PROFILES[0]), 17)
        self.assertEqual(minimum(d, PROFILES[1]), 9)

    def test_edo_access_constraint(self):
        d = [0] * 21
        d[4], d[19] = 14000, 20000
        self.assertEqual(minimum(d, PROFILES[1]), 20000)
        d[20] = 1
        self.assertEqual(minimum(d, PROFILES[1]), 14000)

    def test_check_only_does_not_round_clock(self):
        row = dict(self.rows[0], check=True, rounded=-22)
        self.assertEqual(oracle(row), (0, 291, 12345, 256, 0))

    def test_clock_failure_retains_partial_cache(self):
        row = dict(self.rows[0], rounded=-22)
        value = oracle(row)
        self.assertEqual((value[0], value[3], value[4]), (-22, 256, 1))
        self.assertNotEqual(value[1:3], (291, 12345))

    def test_corrupted_observation_rejected(self):
        row = deepcopy(self.rows[0])
        value = oracle(row)
        obs = dict(id=0, observed=list(value), requested=value[2])
        validate_observations([row], [obs])
        obs["observed"][1] ^= 1
        with self.assertRaises(AssertionError):
            validate_observations([row], [obs])

    def test_lost_clock_call_rejected(self):
        row = deepcopy(self.rows[0])
        obs = dict(id=0, observed=oracle(row), requested=0)
        with self.assertRaises(AssertionError):
            validate_observations([row], [obs])

    def test_profile_drift_rejected(self):
        src = self.source["sunxi_nand.c"].replace(".setup_cycles = 2", ".setup_cycles = 3")
        with self.assertRaisesRegex(ValueError, "model timing profile differs"):
            source_fixture(src, self.source["rawnand.h"], self.rows[:1])

    def test_field_width_drift_rejected(self):
        header = self.source["rawnand.h"].replace("u32 tCLS_min;", "u64 tCLS_min;")
        with self.assertRaisesRegex(ValueError, "field width changed"):
            source_fixture(self.source["sunxi_nand.c"], header, self.rows[:1])

    def test_zero_divisor_control_is_not_tlc_crash(self):
        rows = [dict(r, observed=oracle(r)) for r in self.rows]
        cases = model_cases(rows)
        zero = next(c for c in cases if c["name"] == "timing-mutation-floor-ns")
        self.assertEqual(zero["expected"], "ValidDivisors")
        self.assertTrue(any(minimum(r["d"], PROFILES[1]) < 1000
                            for r in zero["constants"]["Trials"]))

    def test_positive_clock_response_contract(self):
        for row in self.rows:
            if row["group"] == "positive" and row["rounded"] > 0:
                p = minimum(row["d"], PROFILES[row["profile"]])
                self.assertGreaterEqual(10**9 // row["rounded"], (p + 999) // 1000)


if __name__ == "__main__":
    unittest.main()
