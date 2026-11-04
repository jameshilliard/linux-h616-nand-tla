#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Retain the aborted transfer's buffer while observing subsequent commands."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from hardware.run_probe import Probe, RAW, SEEDS, request, verified_backup, restore
from hardware.run_extended import native, checked_native, logical, digest, NREAD, NWRITE

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--directory', type=Path, required=True)
p.add_argument('--variant-matrix', action='store_true')
p.add_argument('--ram-only-confirmed', action='store_true', required=True)
p.add_argument('command', nargs=argparse.REMAINDER)
a = p.parse_args()
directory = a.directory.resolve()
if directory.is_relative_to(Path(__file__).resolve().parents[1]):
    p.error('private evidence must remain outside this repository')
backup = verified_backup(directory)
probe = Probe(a.command)
actual = b''.join(r[2] for r in probe.run([request(1, page=page) for page in range(448, 512)]))
assert actual == backup[448 * RAW:]
report = dict(rows=[], restored=False)
path = directory / ('reset-variants.json' if a.variant_matrix else 'abort-followup.json')
if path.exists():
    p.error('refusing to overwrite evidence')
try:
    probe.run([request(4)])
    data = logical(0)
    checked_native(probe, native(NWRITE, data=data))
    variants = [1, 3, 4, 5] * 24 if a.variant_matrix else [1] * 24
    for index, variant in enumerate(variants):
        rec = probe.run([request(20, seed=SEEDS[0], chunk=(index // 4 % 3), flags=variant)])[0]
        row = dict(index=index, variant=variant, initial_regs=rec[1], initial_sha256=digest(rec[2][:2048]), reads=[])
        for mode in ((1, 1, 0) if index % 2 == 0 else (0, 1, 1)):
            ret, regs, read = checked_native(probe, native(NREAD, mode=mode))
            snapshot = probe.run([request(24)])[0]
            row['reads'].append(dict(mode=mode, result=ret, regs=regs,
                matches=read[:2048] == data[:2048],
                first_bytes=read[:32].hex(), second_step_bytes=read[1024:1056].hex(),
                differences=sum(x != y for x, y in zip(read[:2048], data[:2048])),
                old_buffer_changed=snapshot[2][:2048] != rec[2][:2048],
                old_buffer_now_correct=snapshot[2][:2048] == data[:2048],
                old_buffer_guards=snapshot[1][2]))
        report['rows'].append(row)
        print(index, variant, [(r['mode'], r['matches'], r['old_buffer_changed']) for r in row['reads']], flush=True)
    if a.variant_matrix:
        report['masked_native_reads'] = []
        for mode in (0, 1, 2):
            ret, regs, read = checked_native(probe, native(25, mode=mode))
            report['masked_native_reads'].append(dict(mode=mode, result=ret, regs=regs,
                matches=read[:2048] == data[:2048]))
            print('masked interrupt read', mode, ret, read[:2048] == data[:2048], flush=True)
finally:
    path.write_text(json.dumps(report, indent=2) + '\n')
    restore(probe, directory)
    report['restored'] = True
    path.write_text(json.dumps(report, indent=2) + '\n')
