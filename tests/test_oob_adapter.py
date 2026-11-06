# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep candidate and historical fixture field selection independent."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_c import oob_policy_defines


class PolicyAdapterChecks(unittest.TestCase):
    def test_marker_callback_names(self):
        for name in ("transform_bbm", "compensate_bbm"):
            source = "struct sunxi_nfc_oob_ops {\n    void (*" + name + ")(void);\n};"
            self.assertIn("#define TEST_BBM_CALLBACK " + name, oob_policy_defines(source))

    def test_erased_callback_owner(self):
        for owner in ("sunxi_nfc_oob_ops", "sunxi_nfc_caps"):
            source = ("struct " + owner + " {\n"
                      "    bool (*spare_is_erased)(void);\n};")
            expected = int(owner == "sunxi_nfc_oob_ops")
            self.assertIn(f"#define TEST_SPARE_IN_POLICY {expected}",
                          oob_policy_defines(source))


if __name__ == "__main__":
    unittest.main()
