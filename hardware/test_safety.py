#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-only tests of backup preflight; never opens a device or runs SSH."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import unittest

from .run_probe import RAW, destructive, restore, verified_backup


class NoHardware:
    def run(self, *args, **kwargs):
        raise AssertionError('invalid backups must be rejected before hardware access')


class BackupSafety(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nfc-backup-test-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        # Distinct contents make a replaced partition detectable.
        parts = [bytes([n + 1]) * (128 * RAW) for n in range(4)]
        self.full = b''.join(parts)
        for n, part in enumerate(parts):
            (self.directory / f'nfc-spl-{n}.before').write_bytes(part)
        for chunk in (512, 1024):
            (self.directory / f'module-backup-{chunk}.raw').write_bytes(self.full)

    def rejected(self):
        for operation in (destructive, restore):
            with self.subTest(operation=operation.__name__):
                with self.assertRaises((ValueError, FileNotFoundError)):
                    operation(NoHardware(), self.directory)

    def test_consistent(self):
        self.assertEqual(verified_backup(self.directory), self.full)

    def test_module_mismatch(self):
        (self.directory / 'module-backup-1024.raw').write_bytes(bytes(len(self.full)))
        self.rejected()

    def test_short_matching_module_copies(self):
        for chunk in (512, 1024):
            (self.directory / f'module-backup-{chunk}.raw').write_bytes(self.full[:-1])
        self.rejected()

    def test_each_mtd_mismatch(self):
        for n in range(4):
            with self.subTest(partition=n):
                path = self.directory / f'nfc-spl-{n}.before'
                original = path.read_bytes()
                path.write_bytes(bytes(len(original)))
                self.rejected()
                path.write_bytes(original)

    def test_missing_backup(self):
        # Test-owned temporary file only.
        (self.directory / 'nfc-spl-3.before').unlink()
        self.rejected()

    def test_optimized_python_rejected(self):
        root = Path(__file__).resolve().parent.parent
        result = subprocess.run([sys.executable, '-O', '-m', 'hardware.run_probe', '--help'],
                                cwd=root, text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('validation assertions are required', result.stderr)

    def test_bridge_install_remove(self):
        root = Path(__file__).resolve().parent.parent
        source = self.directory / 'drivers/mtd/nand/raw/sunxi_nand.c'
        source.parent.mkdir(parents=True)
        original = (root / 'reference/sunxi_nand.c').read_bytes()
        source.write_bytes(original)
        command = [sys.executable, str(root / 'hardware/install_bridge.py'),
                   str(self.directory), '--expected-source-sha256', hashlib.sha256(original).hexdigest()]
        subprocess.run(command, check=True, capture_output=True)
        instrumented = source.read_bytes()
        self.assertNotEqual(instrumented, original)
        repeated = subprocess.run(command, capture_output=True)
        self.assertNotEqual(repeated.returncode, 0)
        self.assertEqual(source.read_bytes(), instrumented)
        subprocess.run(command + ['--remove'], check=True, capture_output=True)
        self.assertEqual(source.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
