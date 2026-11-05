#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Instrument a disposable, already-synchronized kernel BUILD tree only.

The four counters observe PIO page/subpage entries and DMA prepare calls.
They do not alter fallback or error handling. Keep the resulting diff with
the private run record. Never run Buildroot source synchronization afterwards
until the experimental kernel/module artifacts have been saved.
"""
import argparse
import hashlib
from pathlib import Path
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('build_tree', type=Path)
p.add_argument('--remove', action='store_true', help='remove only the exact diagnostic instrumentation')
p.add_argument('--expected-source-sha256', help='verify the uninstrumented source before writing')
p.add_argument('--abort-recovery', action='store_true', help='inject one read timeout after DMA launch')
a = p.parse_args()
source = a.build_tree / 'drivers/mtd/nand/raw/sunxi_nand.c'
text = source.read_text()
hooks = {
    'sunxi_nfc_dma_op_prepare': ('\tif (ddir == DMA_FROM_DEVICE)', 2),
    'sunxi_nfc_dma_op_cleanup': ('\twritel(readl(nfc->regs + NFC_REG_CTL) & ~NFC_RAM_METHOD,', 3),
    'sunxi_nfc_hw_ecc_read_page': ('\tret = nand_read_page_op(nand, page, 0, NULL, 0);', 0),
    'sunxi_nfc_hw_ecc_read_subpage': ('\tret = nand_read_page_op(nand, page, 0, NULL, 0);', 0),
    'sunxi_nfc_hw_ecc_write_page': ('\tret = nand_prog_page_begin_op(nand, page, 0, NULL, 0);', 1),
    'sunxi_nfc_hw_ecc_write_subpage': ('\tret = nand_prog_page_begin_op(nand, page, 0, NULL, 0);', 1),
}
declaration = 'static u32 sunxi_nfc_lab_counts[4];\n\n'
include = '\n#include "sunxi_nand_lab.h"\n'
fault_decl = '''#define SUNXI_NFC_LAB_ABORT_RECOVERY
static bool sunxi_nfc_lab_timeout_armed;
static unsigned int sunxi_nfc_lab_timeout_delay;

'''
fault_hook = '''\tif (sunxi_nfc_lab_timeout_armed &&
\t    events == (NFC_CMD_INT_FLAG | NFC_DMA_INT_FLAG)) {
\t\tsunxi_nfc_lab_timeout_armed = false;
\t\tudelay(sunxi_nfc_lab_timeout_delay);
\t\treturn -ETIMEDOUT;
\t}

'''
if a.remove:
    if text.count(declaration) != 1 or not text.endswith(include):
        raise SystemExit('unrecognized diagnostic instrumentation')
    text = text.removesuffix(include).replace(declaration, '', 1)
    if fault_decl in text:
        if text.count(fault_hook) != 1:
            raise SystemExit('unexpected fault hook count')
        text = text.replace(fault_decl, '', 1).replace(fault_hook, '', 1)
    for counter, expected in ((0, 2), (1, 2), (2, 1), (3, 1)):
        line = f'\tsunxi_nfc_lab_counts[{counter}]++;\n'
        if text.count(line) != expected:
            raise SystemExit('unexpected diagnostic counter count')
        text = text.replace(line, '')
    if a.expected_source_sha256 and hashlib.sha256(text.encode()).hexdigest() != a.expected_source_sha256:
        raise SystemExit('restored source hash mismatch; no file changed')
    source.write_text(text)
    print('Removed bridge hooks; unreferenced diagnostic headers retained for inspection.')
    raise SystemExit(0)
if 'sunxi_nfc_lab_counts' in text:
    raise SystemExit('bridge already installed; remove it first')
if a.expected_source_sha256 and hashlib.sha256(text.encode()).hexdigest() != a.expected_source_sha256:
    raise SystemExit('source hash mismatch; no file changed')
if a.abort_recovery:
    if 'bool recovery_failed;' not in text:
        raise SystemExit('requires the private-buffer abort-recovery candidate')
    text = text.replace('static int sunxi_nfc_wait_events(', fault_decl +
                        'static int sunxi_nfc_wait_events(', 1)
    marker = '\tif (events & ~NFC_INT_MASK)'
    assert text.count(marker) == 1
    text = text.replace(marker, fault_hook + marker, 1)
text = text.replace('static int sunxi_nfc_dma_op_prepare(', declaration +
                    'static int sunxi_nfc_dma_op_prepare(', 1)
for name, (statement, counter) in hooks.items():
    start = text.index(name + '(')
    end = text.index('\n}', start)
    body = text[start:end]
    if body.count(statement) != 1:
        raise SystemExit(f'unexpected function body: {name}')
    body = body.replace(statement, f'\tsunxi_nfc_lab_counts[{counter}]++;\n' + statement)
    text = text[:start] + body + text[end:]
text += include
source.write_text(text)
here = Path(__file__).resolve().parent
shutil.copyfile(here / 'native_bridge.h', source.parent / 'sunxi_nand_lab.h')
shutil.copyfile(here / 'probe.h', source.parent / 'sunxi_nand_lab_api.h')
print('Installed test-only bridge and four observation counters.')
