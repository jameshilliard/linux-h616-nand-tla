# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit the finite input domain and pinned source/model correspondence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mtdchar_cases import MODEL, TLC, cases
from run_mtdchar import snapshot, v2_candidate
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


if __name__ == "__main__":
    unittest.main()
