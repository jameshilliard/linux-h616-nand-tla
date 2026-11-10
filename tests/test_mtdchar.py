# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit the finite input domain and pinned source/model correspondence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mtdchar_cases import MODEL, TLC, cases
from run_mtdchar import (TRACE_BATCH_STEPS, immutable, snapshot, trace_batches,
                         trace_cases, v2_candidate)
from run_mtdchar_series import compare, stages
from run_core import extract


class MtdCharInputs(unittest.TestCase):
    def test_stable_toolchain(self):
        self.assertEqual(MODEL["toolchain"], TLC)
        self.assertEqual(TLC["tlc_version"], "1.7.4")
        self.assertEqual(TLC["tlc_sha256"],
                         "936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88")
        self.assertFalse(TLC["tlc_json_traces"])

    def test_profiles(self):
        rows = cases()
        self.assertEqual(len({c["name"] for c in rows}), len(rows))
        for c in rows:
            for r in c["constants"]["Requests"]:
                self.assertGreater(r["width"], 0)
                self.assertGreaterEqual(r["erase"], r["width"])
                self.assertEqual(r["erase"] % r["width"], 0)
                self.assertLessEqual(r["avail"], r["spare"])
                self.assertGreaterEqual(r["avail"], 0)
                self.assertTrue(r["hasData"] or not r["data"])
        self.assertEqual(sum(c["kind"] == "mutation" for c in rows), 7)
        self.assertEqual(sum(c["kind"] == "assumption" for c in rows), 2)

    def test_source_pins(self):
        src, pin = snapshot()
        self.assertEqual(pin["patch_id"], "462a78300fb1dd998dc6fe444527295de712a45c")
        self.assertIn("mtdchar_adjust_io_lengths", src["candidate.c"])
        self.assertNotIn("mtdchar_adjust_io_lengths", src["baseline.c"])
        # Both snapshots deliberately retain the independent admission bug.
        for name in ("baseline.c", "candidate.c"):
            self.assertIn("struct mtd_req_stats stats;", src[name])
            self.assertIn("while (req.len > 0 || (!usr_data && req.ooblen > 0)) {\n"
                          "\t\tstruct mtd_req_stats stats;", src[name])
        core = src["read-wrapper.c"]
        self.assertLess(core.index("mtd_check_oob_ops"), core.index("memset(ops->stats"))

    def test_split_order(self):
        src, _ = snapshot()
        first, second, final = stages(src)
        self.assertEqual(final, src["candidate.c"])
        for source in (first, second):
            self.assertEqual(extract(source, "adjust_oob_length"),
                             extract(final, "adjust_oob_length"))
            self.assertNotIn("mtdchar_adjust_io_lengths", source)
        self.assertEqual(extract(first, "mtdchar_read_ioctl"),
                         extract(src["baseline.c"], "mtdchar_read_ioctl"))
        self.assertNotEqual(extract(first, "mtdchar_read_ioctl"),
                            extract(second, "mtdchar_read_ioctl"))
        self.assertEqual(extract(first, "mtdchar_write_ioctl"),
                         extract(second, "mtdchar_write_ioctl"))

    def test_v2_pin(self):
        src, _ = snapshot()
        candidate = v2_candidate(src["candidate.c"])
        self.assertIn("mtdchar_next_offset", candidate)
        for name in ("mtdchar_read_ioctl", "mtdchar_write_ioctl"):
            body = extract(candidate, name)
            self.assertIn("while (req.len > 0 || req.ooblen > 0)", body)
            self.assertIn(".datbuf = req.len ? datbuf : NULL", body)

    def test_split_regression_gate(self):
        row = dict(width=64, spare=16, avail=8, erase=256, size=4096,
                   read=True, symmetric=True, hasData=False, start=0,
                   data=0, oob=512, good=True)
        self.assertEqual(compare([dict(row, good=False)], [row]), 1)
        with self.assertRaisesRegex(ValueError, "regressed"):
            compare([row], [dict(row, good=False)])
        with self.assertRaisesRegex(ValueError, "differs"):
            compare([row], [dict(row, width=128)])

    def test_trace_batch_boundaries(self):
        rows = [dict(calls=[None] * n) for n in (0, 1, 1, 4, 0)]
        batches = list(trace_batches(rows, max_steps=8))
        self.assertEqual(batches, [rows[:3], rows[3:]])
        self.assertEqual([r for batch in batches for r in batch], rows)
        self.assertEqual(list(trace_batches([], max_steps=8)), [])
        with self.assertRaisesRegex(ValueError, "positive"):
            list(trace_batches(rows, max_steps=0))

    def test_trace_batch_keeps_long_trace(self):
        rows = [dict(calls=[None] * n) for n in (1, 10, 0)]
        batches = list(trace_batches(rows, max_steps=8))
        self.assertEqual(batches, [[row] for row in rows])
        self.assertIs(batches[1][0], rows[1])

    def test_trace_cases_preserve_inputs_and_controls(self):
        rows = [dict(width=width, read=True, hasData=True, data=width,
                     oob=1, calls=[[i * width, width, 1, width, 1, 0]
                                  for i in range(1025)], reported=[width, 1])
                for _ in range(10) for width in (512, 64)]
        baseline = [dict(rows[0], hasData=False, data=0)]
        for complete in (False, True):
            configurations = trace_cases(rows, baseline, complete)
            self.assertEqual(len({c["name"] for c in configurations}),
                             len(configurations))
            positive = [c for c in configurations if c["kind"] == "positive"]
            self.assertGreater(len(positive), 2)
            expected = [immutable(r) for w in (64, 512) for r in rows
                        if r["width"] == w]
            self.assertEqual([r for c in positive for r in c["constants"]["Trials"]],
                             expected)
            for c in positive:
                constants = c["constants"]
                self.assertEqual(constants["CompleteOob"], complete)
                self.assertTrue(constants["CheckPlan"])
                self.assertIsNone(c["expected"])
                self.assertLessEqual(sum(len(r["calls"]) + 2
                                         for r in constants["Trials"]),
                                     TRACE_BATCH_STEPS)
            controls = [c for c in configurations if c["kind"] != "positive"]
            self.assertEqual([c["expected"] for c in controls],
                             ["SourceBytes", "SourcePlan", "SourceBytes", "SourceReturn"])


if __name__ == "__main__":
    unittest.main()
