# SPDX-License-Identifier: GPL-2.0-or-later
"""Negative controls for the offline reset evidence checker; no board access."""
import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest

from hardware.export_reset import pack
from hardware.replay_reset import replay


class ResetEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.capture = json.loads((Path(__file__).with_name('h616-reset-20261104.json')).read_text())

    def rejected(self, capture):
        with tempfile.TemporaryDirectory(prefix='nfc-reset-replay-') as directory:
            path = Path(directory) / 'capture.json'
            path.write_text(json.dumps(capture))
            with self.assertRaises(AssertionError), contextlib.redirect_stdout(io.StringIO()):
                replay(path)

    def test_blob_integrity(self):
        capture = copy.deepcopy(self.capture)
        key = next(iter(capture['blobs']))
        capture['blobs'][key] = '00' + capture['blobs'][key][2:]
        self.rejected(capture)

    def test_silent_corruption_not_hidden(self):
        capture = copy.deepcopy(self.capture)
        row = next(r for r in capture['reports']['reset-diagnosis']['rows']
                   if not r['reads'][0]['matches'] and r['reads'][0]['regs'][1] == 0)
        row['reads'][0]['matches'] = True
        self.rejected(capture)

    def test_control_cannot_be_omitted(self):
        capture = copy.deepcopy(self.capture)
        capture['reports']['reset-bus']['rows'].pop()
        self.rejected(capture)

    def test_unverified_restore_rejected(self):
        capture = copy.deepcopy(self.capture)
        capture['final_verification']['all_spl_restored'] = False
        self.rejected(capture)

    def test_deduplication_is_lossless(self):
        blobs = {}
        original = dict(data='ab' * 2048, abort_data='ab' * 2048)
        encoded = pack(original, blobs)
        self.assertEqual(len(blobs), 1)
        for key in original:
            self.assertEqual(blobs[encoded[key]['blob_sha256']], original[key])


if __name__ == '__main__':
    unittest.main()
