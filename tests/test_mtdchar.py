# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit the finite input domain and pinned source/model correspondence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mtdchar_cases import MODEL, TLC, cases
from run_mtdchar import snapshot


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
        self.assertEqual(pin["patch_id"], "31e5910c9c1ca598ec8872fed0cfffcc579bf867")
        self.assertIn("mtdchar_adjust_io_lengths", src["candidate.c"])
        self.assertNotIn("mtdchar_adjust_io_lengths", src["baseline.c"])
        # Both snapshots deliberately retain the independent admission bug.
        for name in ("baseline.c", "candidate.c"):
            self.assertIn("struct mtd_req_stats stats;", src[name])
            self.assertIn("while (req.len > 0 || (!usr_data && req.ooblen > 0)) {\n"
                          "\t\tstruct mtd_req_stats stats;", src[name])
        core = src["read-wrapper.c"]
        self.assertLess(core.index("mtd_check_oob_ops"), core.index("memset(ops->stats"))


if __name__ == "__main__":
    unittest.main()
