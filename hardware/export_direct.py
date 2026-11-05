#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export an allowlisted, flash-content-free summary of the read-only tests."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from hardware.replay_direct import check


def pick(value, names):
    return {name: value[name] for name in names.split()}


def export(directory):
    raw = json.loads((directory / 'direct-retire.json').read_text())
    native = json.loads((directory / 'native-direct.json').read_text())
    stress = json.loads((directory / 'direct-ecc-stress.json').read_text())
    final = json.loads((directory / 'direct-final.json').read_text())
    native_fields = 'page_in_block mode delay_us result regs matches'
    out = dict(schema=1,
        raw_measured_source_sha256='42b063bbdafd7ca5b1b592c411df84c8a7fcd7d2896b279c78775b195572df7a',
        native_measured_source_sha256='7f93090564302e65e251c48d7a700ec4d20b1b46e2a29d4f777e747f638348cc',
        raw=dict(pick(raw, 'schema read_only unchanged'), rows=[pick(row,
            'repetition mode delay_us same_page result regs next_matches raw_followup_matches')
            for row in raw['rows']]),
        native=dict(pick(native, 'schema read_only selected_pages_in_block programmed_and_erased_controls path_specific_oob_verified'),
            baseline=[pick(row, native_fields) for row in native['baseline']],
            final=[pick(row, native_fields) for row in native['final']],
            rows=[dict(abort=pick(row['abort'], native_fields + ' repetition'),
                       followup=pick(row['followup'], native_fields)) for row in native['rows']]),
        ecc_stress=dict(pick(stress, 'schema read_only page_in_block rounds abort_delay_us'),
            rows=[pick(row, 'repetition mode result regs matches') for row in stress['rows']]),
        final_verification=pick(final, 'all_spl_unchanged ubi_block_unchanged allow_write_disabled '
            'module_unloaded ordinary_mtd_read_passed normal_boot_restored'))
    check(out)
    return out


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('refusing to replace a published capture')
    args.output.write_text(json.dumps(export(args.directory), indent=2) + '\n')
