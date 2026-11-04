#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export the three synthetic reset matrices, never flash backups or logs.

Identical synthetic byte strings are deduplicated, not discarded. Hashes allow
offline replay to check every byte without repeating good page images per row.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

REPORTS = ('reset-diagnosis', 'reset-followup', 'reset-bus')


def pack(value, blobs, key=None):
    if key in ('data', 'abort_data', 'sram'):
        raw = bytes.fromhex(value)
        assert len(raw) in (2048, 2176)
        digest = hashlib.sha256(raw).hexdigest()
        blobs[digest] = value
        return {'blob_sha256': digest}
    if isinstance(value, dict):
        return {k: pack(v, blobs, k) for k, v in value.items()}
    if isinstance(value, list):
        return [pack(v, blobs) for v in value]
    return value


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('private_directory', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    if a.output.exists():
        p.error('refusing to overwrite a capture')
    final = json.loads((a.private_directory / 'final-verification.json').read_text())
    assert all(final[k] for k in ('all_spl_restored', 'module_unloaded', 'ordinary_mtd_read_passed'))
    blobs = {}
    result = dict(schema=1, date='2026-11-04',
        source_sha256='f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6',
        source_note='v10 driver bodies; additive diagnostic SRAM and owned bus-reset helpers',
        geometry=[2048, 128, 131072, 1024, 32, 56, 2],
        reports={}, blobs=blobs, final_verification=final)
    for name in REPORTS:
        report = json.loads((a.private_directory / (name + '.json')).read_text())
        assert report['restored'] and report['rows']
        assert report['pattern'] == 'SHAKE256(nfc-reset-cause-page-N)'
        assert all(row['target'] in (448, 449) for row in report['rows'])
        result['reports'][name] = pack(report, blobs)
    text = json.dumps(result, indent=2) + '\n'
    if re.search(r'/home/|/nvmedata/|\b(?:\d{1,3}\.){3}\d{1,3}\b|sshpass|password|PRIVATE KEY', text, re.I):
        p.error('potential private information in export')
    a.output.write_text(text)
    print(f'{sum(len(r["rows"]) for r in result["reports"].values())} trials, {len(blobs)} unique synthetic buffers')


if __name__ == '__main__':
    main()
