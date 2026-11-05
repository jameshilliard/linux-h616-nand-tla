#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Private raw snapshots for RAM-only fault tests; never exports flash bytes."""
import argparse
from pathlib import Path
import subprocess

from .run_probe import Probe, RAW, request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('transport', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]):
        parser.error('private snapshots must be outside the public repository')
    if not args.transport:
        parser.error('transport required')

    def remote(command):
        return subprocess.check_output(args.transport + [command], timeout=120)

    assert b'rdinit=/init-lab' in remote('cat /proc/cmdline')
    assert not remote('ls -d /sys/class/ubi/ubi[0-9]* 2>/dev/null || true').strip()
    mounts = remote('cat /proc/mounts')
    assert b' ubifs ' not in mounts and b' jffs2 ' not in mounts
    suffix = 'final' if args.verify else 'before'
    for n in range(5):
        name = f'nfc-spl-{n}' if n < 4 else 'nfc-ubi-block'
        target = directory / f'{name}.{suffix}'
        assert not target.exists(), 'refusing to replace an existing snapshot'
        data = remote(f'nanddump -q -n -o --bb=dumpbad /dev/mtd{n}' if n < 4 else
                      'nanddump -q -n -o --bb=dumpbad -l 0x20000 /dev/mtd6')
        assert len(data) == (128 if n < 4 else 64) * RAW
        if args.verify:
            assert data == (directory / f'{name}.before').read_bytes(), name
        target.write_bytes(data)
    if not args.verify:
        assert remote('cat /sys/module/nfc_probe/parameters/allow_write').strip() == b'N'
        probe = Probe(args.transport + ['/tmp/probe_stdio'])
        expected = b''.join((directory / f'nfc-spl-{n}.before').read_bytes() for n in range(4))
        for chunk in (512, 1024):
            target = directory / f'module-backup-{chunk}.raw'
            assert not target.exists()
            data = b''.join(row[2] for row in probe.run([request(1, page=p, chunk=chunk)
                                                        for p in range(512)]))
            if data != expected:
                target.with_suffix('.rejected').write_bytes(data)
                raise AssertionError('independent raw reads differ; rejected observation retained')
            target.write_bytes(data)
    print('Raw data+OOB: all SPL copies and bounded UBI block verified.')


if __name__ == '__main__':
    main()
