#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Allowlisted export/replay of the final streaming-buffer implementation.

Only comparison results and controller counters are public, never flash bytes,
their hashes, target identities, transport commands or private artifact paths.
Offline replay checks a capture; it does not repeat the physical experiment.
"""
import argparse
import itertools
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from hardware.replay_direct import check_native
from hardware.run_native_direct import check_observation

SOURCE = '183c5ff0ad786560110def4fd836bb42b40ff5eb884f1d633496c79e3d17f282'
CAPTURE = Path(__file__).with_name('h616-streaming-implementation-20261104.json')


def check(capture):
    assert capture['schema'] == 1
    assert capture['source_sha256'] == SOURCE
    assert capture['data_policy'] == 'streaming-in-both-dma-modes'
    check_native(capture['native'])
    stress = capture['stress']
    assert stress['schema'] == 1 and stress['read_only'] is True
    assert stress['rounds'] == 100 and stress['abort_delay_us'] == 5
    assert stress['page_in_block'] == 0 and len(stress['rows']) == 400
    for row, profile in zip(stress['rows'], itertools.product(range(100), range(4))):
        assert (row['repetition'], row['mode']) == profile
        check_observation(row)
    final = capture['final_verification']
    for name in ('all_spl_unchanged', 'ubi_block_unchanged', 'allow_write_disabled',
                 'module_unloaded', 'ordinary_mtd_read_passed', 'normal_boot_restored'):
        assert final[name] is True, name


def pick(value, names):
    return {name: value[name] for name in names.split()}


def export(directory):
    native = json.loads((directory / 'native-direct.json').read_text())
    stress = json.loads((directory / 'direct-ecc-stress.json').read_text())
    final = json.loads((directory / 'direct-final.json').read_text())
    fields = 'page_in_block mode delay_us result regs matches'
    out = dict(schema=1, source_sha256=SOURCE, data_policy='streaming-in-both-dma-modes',
        native=dict(pick(native, 'schema read_only selected_pages_in_block programmed_and_erased_controls path_specific_oob_verified'),
            baseline=[pick(r, fields) for r in native['baseline']],
            final=[pick(r, fields) for r in native['final']],
            rows=[dict(abort=pick(r['abort'], fields + ' repetition'),
                       followup=pick(r['followup'], fields)) for r in native['rows']]),
        stress=dict(pick(stress, 'schema read_only page_in_block rounds abort_delay_us'),
            rows=[pick(r, 'repetition mode result regs matches') for r in stress['rows']]),
        final_verification=pick(final, 'all_spl_unchanged ubi_block_unchanged allow_write_disabled '
            'module_unloaded ordinary_mtd_read_passed normal_boot_restored'))
    check(out)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export-directory', type=Path)
    parser.add_argument('--capture', type=Path, default=CAPTURE)
    args = parser.parse_args()
    if args.export_directory:
        if args.capture.exists():
            parser.error('refusing to replace an existing capture')
        args.capture.write_text(json.dumps(export(args.export_directory), indent=2) + '\n')
    check(json.loads(args.capture.read_text()))
    print('Streaming implementation: 128 native abort/retries, 152 controls/followups, '
          '400 stress reads and final non-destructive checks passed')


if __name__ == '__main__':
    main()
