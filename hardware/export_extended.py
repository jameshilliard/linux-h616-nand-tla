#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export only synthetic matrix reports, never backups or transport logs.

The source whitelist is intentional. Review the resulting JSON before publishing.
"""
import argparse
import json
from pathlib import Path
import re

PHASES = ('native', 'context', 'codewords', 'eccregions', 'streams',
          'transitionwrites', 'callers', 'mtd', 'mtdoob', 'stress', 'recovery', 'subpolicy')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('private_directory', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    result = dict(schema=1, date='2026-11-04',
        source_sha256='f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6',
        source_note='v10 function bodies, with native diagnostic bridge and four call counters',
        geometry=[2048, 128, 131072, 1024, 32, 56, 2],
        probe_note='Probe evolved additively during the run; see report for operation provenance.')
    for phase in PHASES:
        report = json.loads((a.private_directory / f'extended-{phase}.json').read_text())
        if not report['restored']:
            p.error(f'{phase}: restoration not verified')
        result[phase] = report
    for name in ('abort-followup', 'reset-variants'):
        report = json.loads((a.private_directory / f'{name}.json').read_text())
        if not report['restored']:
            p.error(f'{name}: restoration not verified')
        result[name] = report
    result['final_verification'] = json.loads(
        (a.private_directory / 'final-verification.json').read_text())
    if not result['final_verification']['all_spl_restored'] or not result['final_verification']['module_unloaded']:
        p.error('finish restoration and unload before exporting')
    text = json.dumps(result, indent=2) + '\n'
    if re.search(r'/home/|/nvmedata/|\b(?:\d{1,3}\.){3}\d{1,3}\b|sshpass|password|PRIVATE KEY', text, re.I):
        p.error('potential private information in export')
    if a.output.exists():
        p.error('refusing to overwrite a published capture')
    a.output.write_text(text)


if __name__ == '__main__':
    main()
