#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export explicit synthetic evidence fields; never copy backups or logs."""
import argparse
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from hardware.replay_native_abort import MEASURED_SOURCE, FINAL_SOURCE, check


def select(record, names):
    return {name: record[name] for name in names.split()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('private_directory', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    if a.output.exists():
        p.error('refusing to replace an existing capture')
    native = json.loads((a.private_directory / 'native-abort.json').read_text())
    stress = json.loads((a.private_directory / 'extended-stress.json').read_text())
    final = json.loads((a.private_directory / 'final-verification.json').read_text())
    safe_native = select(native, 'schema synthetic_pattern injected_timeout restored')
    safe_native['rows'] = []
    for row in native['rows']:
        safe_row = select(row, 'repeat delay_us next_page reader')
        safe_row['observations'] = [select(o, 'result regs matches sha256')
                                    for o in row['observations']]
        safe_native['rows'].append(safe_row)
    safe_stress = select(stress, 'phase restored')
    safe_stress['rows'] = [select(row, 'batch reads bad_data bad_guards fallback failed_ecc')
                           for row in stress['rows']]
    result = dict(schema=1, date='2026-11-04', measured_source_sha256=MEASURED_SOURCE,
                  final_source_sha256=FINAL_SOURCE,
                  source_note='Final source differs only in two continuation indents; '
                              'read-timeout injection and counters are diagnostic-only.',
                  native_abort=safe_native, stress=safe_stress,
                  final_verification=select(final, 'all_spl_restored module_unloaded '
                                           'ordinary_mtd_read_bytes ordinary_mtd_read_passed'))
    check(result)
    text = json.dumps(result, indent=2) + '\n'
    if re.search(r'/home/|/nvmedata/|\b(?:\d{1,3}\.){3}\d{1,3}\b|sshpass|password|PRIVATE KEY',
                 text, re.I):
        p.error('potential private information in export')
    a.output.write_text(text)


if __name__ == '__main__':
    main()
