#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check source-policy identification independently of TLC's outcome checks."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from parser_reference import evidence, profiles, reference_kind
from run_lifetime_publication import cases


class ParserReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = profiles()

    def test_pinned_policies(self):
        self.assertEqual(reference_kind(self.sources["v5"]["body"]), "operational")
        self.assertEqual(reference_kind(self.sources["v6"]["body"]), "metadata")

    def test_comments_are_not_calls(self):
        comment = "/* get_mtd_device(child, -1); put_mtd_device(child); */\n"
        self.assertEqual(reference_kind(comment + self.sources["v6"]["body"]), "metadata")

    def test_missing_and_mismatched_put_are_rejected(self):
        for replacement in ("", "put_mtd_device(child);"):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                reference_kind(self.sources["v6"]["body"].replace("mtd_put_data(child);",
                                                                  replacement))

    def test_reference_must_precede_unlock(self):
        body = self.sources["v6"]["body"].replace(
            "mtd_get_data(child);\n\t\tmutex_unlock(&master->master.partitions_lock);",
            "mutex_unlock(&master->master.partitions_lock);\n\t\tmtd_get_data(child);")
        self.assertNotEqual(body, self.sources["v6"]["body"])
        with self.assertRaises(ValueError):
            reference_kind(body)

    def test_early_put_is_rejected(self):
        body = self.sources["v6"]["body"].replace("mtd_put_data(child);", "")
        body = body.replace("mtd_get_data(child);", "mtd_get_data(child); mtd_put_data(child);")
        with self.assertRaises(ValueError):
            reference_kind(body)

    def test_missing_error_path_is_rejected(self):
        body = self.sources["v5"]["body"].replace(
            "ret = PTR_ERR_OR_ZERO(get_mtd_device(child, -1));\n\t\tif (ret)\n\t\t\tgoto err_unlock;",
            "ret = PTR_ERR_OR_ZERO(get_mtd_device(child, -1));")
        self.assertNotEqual(body, self.sources["v5"]["body"])
        with self.assertRaises(ValueError):
            reference_kind(body)

    def test_source_profiles_select_model_policy(self):
        for name in ("v5", "v6"):
            matrix = cases(dict(self.sources, current=self.sources[name]))
            case = next(c for c in matrix if c["name"] == "parser-current-source")
            self.assertEqual(case["constants"]["ParserPin"], self.sources[name]["reference_kind"])
            self.assertEqual(case["expected"], "RegistrationIndependent" if name == "v5" else None)

    def test_case_names_and_positive_controls(self):
        matrix = cases(self.sources)
        self.assertEqual(len(matrix), len({c["name"] for c in matrix}))
        new = [c for c in matrix if c["module"] == "MtdParserReferences"]
        self.assertEqual(len(new), 22)
        self.assertEqual(sum(c["kind"] == "positive" for c in new), 14)
        self.assertEqual(sum(c["kind"] == "regression" for c in new), 2)
        self.assertEqual(sum(c["kind"] == "mutation" for c in new), 6)

    def test_evidence_omits_bodies_and_private_paths(self):
        for item in evidence(self.sources).values():
            self.assertNotIn("body", item)
            self.assertFalse(Path(item["path"]).is_absolute())


if __name__ == "__main__":
    unittest.main()
