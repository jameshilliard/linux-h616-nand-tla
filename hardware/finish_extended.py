#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only final backup comparison, module unload and ordinary MTD read.

Pass the SSH transport without a remote command. The four NAND dumps stay in
the private evidence directory; only boolean verification results are exported.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from hardware.run_probe import Probe, RAW, request, verified_backup


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('transport', nargs=argparse.REMAINDER)
    a = p.parse_args()
    directory = a.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]):
        p.error('private evidence must remain outside this repository')
    if not a.transport:
        p.error('SSH transport required')
    path = directory / 'final-verification.json'
    if path.exists():
        p.error('refusing to overwrite final verification')
    original = verified_backup(directory)
    probe = Probe(a.transport + ['/tmp/probe_stdio'])
    actual = b''.join(r[2] for r in probe.run([request(1, page=n) for n in range(512)]))
    assert original == actual, 'final module raw read differs from backup'
    for n in range(4):
        result = subprocess.run(a.transport + [f'nanddump -q -n -o --bb=dumpbad /dev/mtd{n}'],
                                stdout=subprocess.PIPE, check=True, timeout=120)
        assert result.stdout == original[n * 128 * RAW:(n + 1) * 128 * RAW], n
        (directory / f'nfc-spl-{n}.after').write_bytes(result.stdout)
    subprocess.run(a.transport + ['rmmod nfc_probe && test ! -d /sys/module/nfc_probe'],
                   check=True, timeout=60)
    result = subprocess.run(a.transport + ['time -p dd if=/dev/mtd6 of=/dev/null bs=1M count=32'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    (directory / 'ordinary-read-after.log').write_bytes(result.stdout)
    verification = dict(all_spl_restored=True, module_unloaded=True,
                        ordinary_mtd_read_bytes=32 * 1024 * 1024,
                        ordinary_mtd_read_passed=result.returncode == 0)
    path.write_text(json.dumps(verification, indent=2) + '\n')
    assert result.returncode == 0, 'ordinary MTD read failed; inspect private log'
    print('All four raw SPL copies verified; module unloaded; ordinary 32 MiB read passed.')


if __name__ == '__main__':
    main()
