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
p.add_argument('--request-faults', action='store_true',
               help='also observe pre-confirm writes and real PIO timeout cleanup')
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
private_decl = '#define SUNXI_NFC_LAB_PRIVATE_BUFFER\n'
include = '\n#include "sunxi_nand_lab.h"\n'
fault_decl = '''#define SUNXI_NFC_LAB_ABORT_RECOVERY
static bool sunxi_nfc_lab_timeout_armed;
static unsigned int sunxi_nfc_lab_timeout_delay;
static u32 sunxi_nfc_lab_timeout_regs[3];

'''
fault_hook = '''\tif (sunxi_nfc_lab_timeout_armed &&
\t    events == (NFC_CMD_INT_FLAG | NFC_DMA_INT_FLAG)) {
\t\tsunxi_nfc_lab_timeout_armed = false;
\t\tudelay(sunxi_nfc_lab_timeout_delay);
\t\tsunxi_nfc_lab_timeout_regs[0] = readl(nfc->regs + NFC_REG_ST);
\t\tsunxi_nfc_lab_timeout_regs[1] = readl(nfc->regs + NFC_REG_H6_MDMA_STA);
\t\tsunxi_nfc_lab_timeout_regs[2] = readl(nfc->regs + 0x214);
\t\treturn -ETIMEDOUT;
\t}

'''
request_decl = '''#define SUNXI_NFC_LAB_REQUEST_FAULTS
static u32 sunxi_nfc_lab_confirms;
static unsigned int sunxi_nfc_lab_pio_stage;
static unsigned int sunxi_nfc_lab_pio_delay;
static bool sunxi_nfc_lab_pio_wait;
static u32 sunxi_nfc_lab_pio_regs[4];

'''
request_wait_decl = '''\tbool lab_early = sunxi_nfc_lab_pio_wait;

\tif (lab_early) {
\t\tsunxi_nfc_lab_pio_wait = false;
\t\tsunxi_nfc_lab_pio_regs[0] = readl(nfc->regs + NFC_REG_ST);
\t\tsunxi_nfc_lab_pio_regs[1] = readl(nfc->regs + NFC_REG_CMD);
\t\tsunxi_nfc_lab_pio_regs[2] = readl(nfc->regs + NFC_REG_CNT);
\t\tudelay(sunxi_nfc_lab_pio_delay);
\t}

'''
request_wait_exit = '''\tif (lab_early) {
\t\tsunxi_nfc_lab_pio_regs[3] = readl(nfc->regs + NFC_REG_ST);
\t\t/* Balance the lab's pre-command IRQ disable, not the driver's. */
\t\tenable_irq(nfc->irq);
\t}

'''
confirm_hook = '''\tif (((cmd & NFC_SEND_CMD1) && (cmd & 0xff) == NAND_CMD_PAGEPROG) ||
\t    ((cmd & NFC_SEND_CMD2) && (extcmd & 0xff) == NAND_CMD_PAGEPROG))
\t\tsunxi_nfc_lab_confirms++;

'''
def pio_hook(stage, indent):
    return '\n'.join(indent + line if line else '' for line in f'''if (sunxi_nfc_lab_pio_stage == {stage}) {{
\tsunxi_nfc_lab_pio_stage = 0;
\tdisable_irq(nfc->irq);
\tsunxi_nfc_lab_pio_wait = true;
}}
'''.split('\n'))

def change_body(content, name, old, new):
    start = content.index(name + '(')
    end = content.index('\n}', start)
    body = content[start:end]
    if body.count(old) != 1:
        raise SystemExit(f'unexpected request-fault anchor in {name}')
    return content[:start] + body.replace(old, new) + content[end:]

request_edits = [
    ('sunxi_nfc_wait_events', '\tint ret;\n', '\tint ret;\n' + request_wait_decl),
    ('sunxi_nfc_wait_events', 'ret = wait_for_completion_timeout(',
     'ret = lab_early ? 0 : wait_for_completion_timeout('),
    ('sunxi_nfc_wait_events', '\tif (ret)\n\t\tdev_err',
     request_wait_exit + '\tif (ret)\n\t\tdev_err'),
    ('sunxi_nfc_read_buf', '\t\twritel(tmp, nfc->regs + NFC_REG_CMD);',
     pio_hook(1, '\t\t') + '\t\twritel(tmp, nfc->regs + NFC_REG_CMD);'),
    ('sunxi_nfc_hw_ecc_read_chunk', '\twritel(NFC_DATA_TRANS | NFC_DATA_SWAP_METHOD | NFC_ECC_OP,',
     pio_hook(2, '\t') + '\twritel(NFC_DATA_TRANS | NFC_DATA_SWAP_METHOD | NFC_ECC_OP,'),
    ('sunxi_nfc_exec_subop', '\twritel(cmd, nfc->regs + NFC_REG_CMD);',
     confirm_hook + '\twritel(cmd, nfc->regs + NFC_REG_CMD);'),
]
if a.remove:
    if text.count(declaration) != 1 or not text.endswith(include):
        raise SystemExit('unrecognized diagnostic instrumentation')
    text = text.removesuffix(include).replace(declaration, '', 1)
    text = text.replace(private_decl, '', 1)
    if request_decl in text:
        for name, old, new in reversed(request_edits):
            text = change_body(text, name, new, old)
        text = text.replace(request_decl, '', 1)
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
        raise SystemExit('requires the H616 abort-recovery candidate')
    if 'void *mdma_buf;' in text or '\tvoid *mdma_buf;' in text:
        text = private_decl + text
    text = text.replace('static int sunxi_nfc_wait_events(', fault_decl +
                        'static int sunxi_nfc_wait_events(', 1)
    marker = '\tif (events & ~NFC_INT_MASK)'
    assert text.count(marker) == 1
    text = text.replace(marker, fault_hook + marker, 1)
if a.request_faults:
    if not a.abort_recovery:
        raise SystemExit('--request-faults requires --abort-recovery; no file changed')
    text = text.replace(fault_decl, request_decl + fault_decl, 1)
    for name, old, new in request_edits:
        text = change_body(text, name, old, new)
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
shutil.copyfile(here / 'native_direct.h', source.parent / 'sunxi_nand_lab_direct.h')
shutil.copyfile(here / 'native_faults.h', source.parent / 'sunxi_nand_lab_faults.h')
print('Installed test-only bridge and four observation counters.')
