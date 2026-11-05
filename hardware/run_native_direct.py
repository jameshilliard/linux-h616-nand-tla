#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only ECC/streaming-DMA comparison; never publish existing flash data.

Requires the abort-recovery diagnostic bridge in a separately booted RAM
image, UBI detached, and module allow_recovery=1, allow_write=0. The bounded
block is already populated; this program issues no erase/program requests.
"""
import argparse
import itertools
import json
from pathlib import Path

if __package__:
    from .run_probe import Probe, RAW, SEEDS, request
else:
    from run_probe import Probe, RAW, SEEDS, request
from codec_oracle import scramble


def expected_pio_parity(dma_data, page):
    """Preserved PIO-decoded versus DMA-raw parity, checked byte for byte."""
    state = SEEDS[page % 64]
    for _ in range(1024 * 8 + 15):
        state = (state >> 1) | (((state ^ (state >> 1)) & 1) << 14)
    expected = bytearray(dma_data)
    for offset, user in ((2056, 8), (2112, 0)):
        key = scramble(bytes(user + 56), state)[user:]
        expected[offset:offset + 56] = bytes(
            value ^ mask for value, mask in zip(dma_data[offset:offset + 56], key))
    return bytes(expected)


def check_observation(row):
    regs = row['regs']
    mode = row['mode']
    assert len(regs) == 16 and mode in range(4), row
    assert row['result'] >= 0 and row['matches'], row
    assert regs[1] == 0 and regs[5] == 1, row
    assert regs[6:10] == [1] * 4, row
    assert regs[10:16] == [int(mode in (0, 3)), 0, int(mode != 0),
                           int(mode != 0), 1, 0xd200 | mode], row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--repeat', type=int, default=8)
    parser.add_argument('--ram-only-confirmed', action='store_true', required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]):
        parser.error('private evidence must be outside the public repository')
    if not args.command or not 1 <= args.repeat <= 100:
        parser.error('transport and bounded repeat required')
    path = directory / 'native-direct.json'
    if path.exists():
        parser.error('refusing to replace evidence')
    # Independent raw backup is required before any reset injection.
    raw = (directory / 'nfc-ubi-block.before').read_bytes()
    assert len(raw) == 64 * RAW
    probe = Probe(args.command)
    assert probe.run([request(0)])[0][1][:7] == [2048, 128, 131072, 1024, 32, 56, 2]
    report = dict(schema=1, read_only=True, baseline=[], rows=[], final=[])
    reference = {}
    pages = (2688, 2689, 2690, 2691)

    def observe(page, mode, delay=0):
        result, regs, data = probe.run([request(29, page=page, length=2048,
            seed=mode, chunk=delay)], accept_errors=True)[0]
        if mode == 0 and page not in reference:
            assert result >= 0 and regs[1] == 0, (page, result, regs)
            reference[page] = {0: data}
        if mode == 1 and 1 not in reference[page]:
            assert result >= 0 and regs[1] == 0, (page, result, regs)
            stored = raw[(page - 2688) * RAW:(page - 2688 + 1) * RAW]
            if stored == b'\xff' * RAW:
                assert data == reference[page][0] == stored
            else:
                assert data[2056:2168] == stored[2056:2168], 'DMA parity differs from raw backup'
                assert expected_pio_parity(data, page) == reference[page][0], 'PIO parity representation mismatch'
            reference[page][1] = data
        expected = reference[page][int(mode in (1, 2))]
        return dict(page_in_block=page - 2688, mode=mode, delay_us=delay,
                    result=result, regs=regs, matches=data == expected), data

    for page, mode in itertools.product(pages, (0, 1, 2)):
        row, data = observe(page, mode)
        report['baseline'].append(row)
        path.write_text(json.dumps(report, indent=2) + '\n')
        check_observation(row)
    nonuniform = [page for page in pages if len(set(reference[page][0][:2048])) > 8]
    erased = [page for page in pages if reference[page][0] == b'\xff' * RAW]
    assert nonuniform and erased, 'need a programmed page and an erased page'
    chosen = [nonuniform[0], erased[0]]
    report['selected_pages_in_block'] = [page - 2688 for page in chosen]
    report['programmed_and_erased_controls'] = True
    report['path_specific_oob_verified'] = True
    for repetition, delay, page in itertools.product(range(args.repeat),
            (0, 1, 2, 5, 10, 20, 50, 1000), chosen):
        row, _ = observe(page, 3, delay)
        row['repetition'] = repetition
        report['rows'].append(dict(abort=row))
        path.write_text(json.dumps(report, indent=2) + '\n')
        # Stop before further access if recovery or buffer ownership failed.
        check_observation(row)
        other = chosen[1] if page == chosen[0] else chosen[0]
        followup, _ = observe(other, 2)
        report['rows'][-1]['followup'] = followup
        path.write_text(json.dumps(report, indent=2) + '\n')
        check_observation(followup)
        print(len(report['rows']), 'delay', delay, 'pre-ST/count',
              row['regs'][2], row['regs'][4], 'PASS', flush=True)
    for page, mode in itertools.product(pages, (0, 1, 2)):
        row, _ = observe(page, mode)
        report['final'].append(row)
        path.write_text(json.dumps(report, indent=2) + '\n')
        check_observation(row)


if __name__ == '__main__':
    main()
