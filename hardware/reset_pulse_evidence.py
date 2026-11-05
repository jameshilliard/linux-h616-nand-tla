#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export/replay read-only abort/reuse checks of the CCU-owned reset pulse.

Only source digests, controller observations and comparison results are public.
No flash bytes, flash-content hashes, target identities or transport details
are exported. Replay validates recorded evidence, not a new hardware run.
"""
import argparse
import hashlib
import json
from pathlib import Path

from hardware.replay_direct import check_native
from hardware.streaming_implementation import pick

SOURCE = '3d5176b4266d14a79782e6bbcb3e7cfea39ff347b6772db85112f600c4f103f7'
PROVIDER = 'c895dfdaf2a1af2b5aeb28cc43070f40304b51b49f71f35223274e3bf778fef6'
CAPTURE = Path(__file__).with_name('h616-reset-pulse-20261105.json')
FINAL_FIELDS = ('all_spl_unchanged ubi_block_unchanged allow_write_disabled '
                'module_unloaded ordinary_mtd_read_passed normal_boot_restored')


def check(capture):
    assert capture['schema'] == 1
    assert capture['source_sha256'] == SOURCE
    assert capture['provider_source_sha256'] == PROVIDER
    assert capture['data_policy'] == 'streaming-in-both-dma-modes'
    assert capture['reset_api'] == 'reset_control_reset'
    assert capture['provider_delay_us'] == 10
    check_native(capture['native'])
    for name in FINAL_FIELDS.split():
        assert capture['final_verification'][name] is True, name


def export(directory):
    for name, expected in (('production-sunxi_nand.c', SOURCE),
                           ('production-ccu_reset.c', PROVIDER)):
        assert hashlib.sha256((directory / name).read_bytes()).hexdigest() == expected
    native = json.loads((directory / 'native-direct.json').read_text())
    final = json.loads((directory / 'direct-final.json').read_text())
    fields = 'page_in_block mode delay_us result regs matches'
    out = dict(schema=1, source_sha256=SOURCE, provider_source_sha256=PROVIDER,
        data_policy='streaming-in-both-dma-modes', reset_api='reset_control_reset',
        provider_delay_us=10,
        native=dict(pick(native, 'schema read_only selected_pages_in_block '
                                'programmed_and_erased_controls path_specific_oob_verified'),
            baseline=[pick(r, fields) for r in native['baseline']],
            final=[pick(r, fields) for r in native['final']],
            rows=[dict(abort=pick(r['abort'], fields + ' repetition'),
                       followup=pick(r['followup'], fields)) for r in native['rows']]),
        final_verification=pick(final, FINAL_FIELDS))
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
    print('CCU reset pulse: 128 native abort/retries, 152 controls/followups '
          'and final non-destructive checks passed')


if __name__ == '__main__':
    main()
