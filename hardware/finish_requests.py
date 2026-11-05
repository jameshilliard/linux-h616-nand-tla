#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Finish a restored request-fault experiment with ordinary-kernel reads."""
import argparse
import json
from pathlib import Path
import subprocess

from .run_probe import RAW, verified_backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--normal-boot', action='store_true')
    parser.add_argument('transport', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]) or not args.transport:
        parser.error('private directory and transport required')

    def remote(command, **kw):
        return subprocess.check_output(args.transport + [command], timeout=180, **kw)

    path = directory / 'request-final.json'
    if args.normal_boot:
        report = json.loads(path.read_text())
        state = remote('uname -a && cat /proc/cmdline && test -e /sys/class/ubi/ubi0 && '
                       'test ! -d /sys/module/nfc_probe')
        assert b'rdinit=/init-lab' not in state
        (directory / 'normal-boot.log').write_bytes(state)
        report['normal_boot_restored'] = True
    else:
        assert not path.exists()
        verified_backup(directory)
        for name in ('nfc-spl-0', 'nfc-spl-1', 'nfc-spl-2', 'nfc-spl-3', 'nfc-ubi-block'):
            assert (directory / f'{name}.final').read_bytes() == (directory / f'{name}.before').read_bytes()
        tests = json.loads((directory / 'request-faults.json').read_text())
        assert tests['complete'] and tests['restored']
        state = remote('uname -a && cat /proc/cmdline && test ! -d /sys/module/nfc_probe && '
                       '! test -e /sys/class/ubi/ubi0')
        assert b'rdinit=/init-lab' in state
        # The plain Image must have no exported observation bridge.
        assert b'sunxi_nfc_lab_' not in remote('cat /proc/kallsyms')
        (directory / 'ordinary-state.log').write_bytes(state)
        before = [int(remote(f'cat /sys/class/mtd/mtd6/{field}'))
                  for field in ('corrected_bits', 'ecc_failures')]
        for repeat in range(3):
            data = remote('time -p dd if=/dev/mtd6 of=/dev/null bs=1M count=64',
                          stderr=subprocess.STDOUT)
            (directory / f'ordinary-read-{repeat}.log').write_bytes(data)
            assert b'64+0 records in' in data and b'64+0 records out' in data
        after = [int(remote(f'cat /sys/class/mtd/mtd6/{field}'))
                 for field in ('corrected_bits', 'ecc_failures')]
        assert after[1] == before[1]
        raw = remote('nanddump -q -n -o --bb=dumpbad -s 0x20000 -l 0x20000 /dev/mtd3')
        assert raw == (directory / 'nfc-spl-3.before').read_bytes()[64 * RAW:]
        (directory / 'ordinary-scratch.raw').write_bytes(raw)
        dmesg = remote('dmesg')
        (directory / 'ordinary-dmesg.log').write_bytes(dmesg)
        assert b'wait interrupt timedout' not in dmesg
        assert b'AHB reset failed' not in dmesg
        assert b'BUG:' not in dmesg and b'WARNING:' not in dmesg
        report = dict(all_spl_restored=True, ubi_block_unchanged=True,
            diagnostic_module_unloaded=True, ordinary_kernel_has_no_test_hooks=True,
            ordinary_read_bytes=64 * 1024 * 1024, ordinary_read_count=3,
            ordinary_ecc_corrected_delta=after[0] - before[0],
            ordinary_ecc_failed_delta=after[1] - before[1],
            ordinary_scratch_verified=True, normal_boot_restored=False)
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
