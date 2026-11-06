#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check ordinary-format marker handling across late manufacturer setup.

The pinned submission is a negative control. A candidate must pass unless
--expect-late-init-bug explicitly requests that same regression signature.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from boundary_common import HERE, check
from run_c import extract_function, oob_policy_defines, oob_policy_functions, oob_policy_prototype, oob_table_names


def fixture(source, *, fixed, extra_functions=()):
    """Extract unchanged policy/marker helpers for both marker test runners."""
    toshiba = (HERE / 'reference/toshiba-bbm-init.c').read_text()
    manufacturer = json.loads((HERE / 'reference/bbm-init.json').read_text())
    assert hashlib.sha256(toshiba.encode()).hexdigest() == manufacturer['sha256']
    has_vendor = 'sunxi_nfc_allwinner_oob_attach_chip' in source
    has_ops = 'struct sunxi_nfc_oob_ops {' in source
    has_policy = 'sunxi_nfc_get_oob_ops(' in source
    deferred = 'sunxi_nfc_select_oob_ops(' in source
    generated = [f'#define HAS_VENDOR {int(has_vendor)}\n#define EXPECT_FIXED {int(fixed)}\n'
                 f'#define HAS_OPS_POLICY {int(has_policy)}\n'
                 f'#define DEFERRED_SELECTION {int(deferred)}\n' + oob_policy_defines(source),
                 (HERE / 'tests/bbm_init.h').read_text()]
    hashes = {}
    if has_ops:
        start = source.index('struct sunxi_nfc_oob_ops {')
        generated.append(source[start:source.index('\n};', start) + 3])
    generated.append(oob_policy_prototype(source))
    for name in ('sunxi_nfc_randomizer_page_seeds', 'sunxi_nfc_randomizer_ecc512_seeds',
                 'sunxi_nfc_randomizer_ecc1024_seeds'):
        generated.append(re.search(r'^static const u16 ' + name + r'\[\] = \{.*?\n};',
                                   source, re.M | re.S)[0])
    def add(text, filename, name):
        body = extract_function(text, name)
        line = text[:text.index(body)].count('\n') + 1
        hashes[f'{filename}:{name}'] = hashlib.sha256(body.encode()).hexdigest()
        generated.append(f'#line {line} "{filename}"\n' + body)
    for name in ('sunxi_nfc_randomizer_step', 'sunxi_nfc_randomizer_state',
                 'sunxi_nfc_randomizer_config', 'sunxi_nfc_randomizer_enable',
                 'sunxi_nfc_randomizer_disable', 'sunxi_nfc_randomize_bbm',
                 'sunxi_nfc_user_data_to_buf', 'sunxi_nfc_buf_to_user_data',
                 'sunxi_nfc_user_data_sz', 'sunxi_nfc_hw_ecc_get_prot_oob_bytes',
                 'sunxi_nfc_hw_ecc_set_prot_oob_bytes'):
        add(source, 'sunxi_nand.c', name)
    for name in oob_table_names(source):
        match = re.search(r'^static const struct sunxi_nfc_oob_ops ' + name +
                          r' = \{.*?\n};', source, re.M | re.S)
        if match:
            generated.append(match[0])
    for name in oob_policy_functions(source):
        add(source, 'sunxi_nand.c', name)
    if has_vendor:
        add(source, 'sunxi_nand.c', 'sunxi_nfc_allwinner_oob_attach_chip')
    add(source, 'sunxi_nand.c', 'sunxi_nand_attach_chip')
    add(toshiba, 'toshiba-bbm-init.c', 'tc58teg5dclta00_init')
    for name in extra_functions:
        add(source, 'sunxi_nand.c', name)
    return generated, hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate-source', type=Path)
    parser.add_argument('--expect-late-init-bug', action='store_true')
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    pin = json.loads((HERE / 'source.json').read_text())
    source = (HERE / 'reference/sunxi_nand.c').read_text()
    assert hashlib.sha256(source.encode()).hexdigest() == pin['driver_sha256']
    if args.candidate_source:
        source = args.candidate_source.read_text()
    generated, hashes = fixture(source, fixed=bool(args.candidate_source) and
                               not args.expect_late_init_bug)
    generated += ['#line 1 "bbm_init.c"', (HERE / 'tests/bbm_init.c').read_text()]
    check('bbm-init', {'check.c': '\n\n'.join(generated)}, hashes, cc=args.cc,
          expected='ALL BBM INIT CHECKS PASSED', limits=[
              'MMIO and geometry setup are fixtures; no physical device is tested.',
              'This marker test explicitly calls final selection; run_lifecycle.py checks actual scan/BBT ordering.',
              'Manufacturer flag mutation uses the pinned Toshiba init body.',
              'Checks old-image reads and compensated writes, not only round trips.',
              'Does not execute complete PIO/DMA transfers or BCH correction.'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
