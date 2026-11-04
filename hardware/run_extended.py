#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicitly destructive bounded H616 matrices; independent raw backup required.

Run only in the documented RAM-only environment with UBI unattached. Recovery
is opt-in read-side fault injection. Synthetic inputs/results may be published; raw
backups, board identity and transport arguments must remain private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from hardware.run_probe import (Probe, RAW, SEEDS, request, verified_backup, restore,
                                measured_stream, page_image)
from codec_oracle import encode, scramble, REVERSE

READ, ERASE, PROGRAM = 1, 4, 5
NREAD, NWRITE, NSUBREAD, NSUBWRITE, NOOBREAD, NOOBWRITE, NRAWREAD, NRAWWRITE = range(10, 18)
CODEWORD, TRANSITIONS = 18, 19
STRENGTHS = (16, 24, 28, 32, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def logical(index):
    return bytes((i * 37 + (i >> 8) * 13 + index * 7) & 255 for i in range(RAW))


def native(op, page=448, mode=1, column=0, length=2048, data=None, flags=64):
    return request(op, page=page, column=column, length=length, seed=mode,
                   flags=flags,
                   data=data if data is not None else b'\xa5' * RAW)


def checked_native(probe, req):
    result, regs, data = probe.run([req])[0]
    assert regs[14:] in ([1, 16], [1, 17]), ('native bridge/guards', regs)
    return result, regs, data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--phase', choices=('native', 'context', 'codewords', 'eccregions', 'streams', 'transitionwrites', 'callers', 'stress', 'mtd', 'mtdoob', 'recovery', 'subpolicy'), required=True)
    p.add_argument('--ram-only-confirmed', action='store_true', required=True)
    p.add_argument('command', nargs=argparse.REMAINDER)
    a = p.parse_args()
    directory = a.directory.resolve()
    if directory.is_relative_to(Path(__file__).resolve().parents[1]):
        p.error('private evidence must be outside the public repository')
    if not a.command:
        p.error('transport command required')
    backup = verified_backup(directory)
    probe = Probe(a.command)
    actual = b''.join(r[2] for r in probe.run([request(READ, page=page) for page in range(448, 512)]))
    assert actual == backup[448 * RAW:], 'scratch differs from independent backups'
    report = dict(phase=a.phase, rows=[], restored=False, native_stale_metadata_primed=True)
    path = directory / f'extended-{a.phase}.json'
    if path.exists():
        p.error('refusing to overwrite a run report')
    for op in (NWRITE, NSUBWRITE, NOOBWRITE, NRAWWRITE):
        assert probe.run([native(op, page=0)], accept_errors=True)[0][0] == -30
    started = time.monotonic()
    try:
        probe.run([request(ERASE)])
        if a.phase == 'native':
            for index in range(64):
                page, data, mode = 448 + index, logical(index), index % 2
                wr = checked_native(probe, native(NWRITE, page, mode, data=data))
                raw = probe.run([request(READ, page=page)])[0][2]
                row = dict(index=index, writer=mode, write_counts=wr[1][10:14],
                           physical_matches_oracle=raw == page_image(data, page),
                           physical_sha256=digest(raw), reads=[])
                for reader in (0, 1, 2):
                    ret, regs, decoded = checked_native(probe, native(NREAD, page, reader))
                    row['reads'].append(dict(mode=reader, result=ret, regs=regs,
                        main_matches=decoded[:2048] == data[:2048],
                        user_matches=decoded[2048:2056] == data[2048:2056],
                        tail_matches=decoded[2168:] == data[2168:]))
                report['rows'].append(row)
                if index % 8 == 7:
                    print('native pages', index + 1, flush=True)
        elif a.phase == 'context':
            for index in range(8):
                page, data, mode = 448 + index, logical(index), index % 2
                written = checked_native(probe, native(NWRITE, page, mode, data=data, flags=96))
                row = dict(index=index, writer=mode, write_regs=written[1], reads=[])
                for reader in (0, 1, 2):
                    ret, regs, read = checked_native(probe, native(NREAD, page, reader, flags=96))
                    row['reads'].append(dict(mode=reader, result=ret, regs=regs,
                        main_matches=read[:2048] == data[:2048],
                        user_matches=read[2048:2056] == data[2048:2056],
                        tail_matches=read[2168:] == data[2168:]))
                report['rows'].append(row)
            print('all four stale user-length registers', len(report['rows']), flush=True)
        elif a.phase == 'codewords':
            cases = [(strength, 8, errors, True) for strength in STRENGTHS
                     for errors in (0, 1, strength)]
            cases += [(32, user, 1, True) for user in range(0, 33, 4)]
            cases += [(32, 8, n, True) for n in (33, 40, 65)]
            # Non-randomized blank/pattern classification is separate from BCH.
            cases += [(32, 8, value, False) for value in (0, 255)]
            for index, (strength, user, errors, random) in enumerate(cases):
                page, seed = 448 + index, SEEDS[index]
                clean = logical(index)[:1024] + logical(index)[2048:2048 + user]
                parity = encode(clean, strength)
                parity += b'\xff' * (len(parity) % 2)
                code = bytearray(scramble(clean + parity, seed) if random else bytes([errors]) * (1024 + user + len(parity)))
                if random:
                    for bit in range(errors):
                        # Distinct spread-out positions, mostly in main data.
                        where = bit * 107 % ((1024 + user + len(encode(clean, strength))) * 8)
                        code[where // 8] ^= 1 << (where % 8)
                image = bytes(code).ljust(RAW, b'\xff')
                probe.run([request(PROGRAM, page=page, data=image)])
                req = request(CODEWORD, page=page, length=1024, seed=seed,
                              flags=9 if random else 8,
                              chunk=STRENGTHS.index(strength) | user << 8 | 1024 << 16)
                ret, regs, data = probe.run([req])[0]
                report['rows'].append(dict(strength=strength, user=user, errors=errors,
                    randomized=random, regs=regs[:5], result=ret,
                    main_matches=data[:1024] == clean[:1024],
                    user_matches=data[2048:2048 + user] == clean[1024:],
                    decoded_sha256=digest(data[:1024])))
                print('codeword', index, strength, user, errors, regs[:4], flush=True)
        elif a.phase == 'eccregions':
            cases = [(t, 8, region) for t in STRENGTHS
                     for region in ('main', 'user', 'parity')]
            cases += [(32, user, 'user') for user in range(4, 33, 4)]
            for index, (strength, user, region) in enumerate(cases):
                page, seed = 448 + index, SEEDS[index]
                clean = logical(index)[:1024] + logical(index)[2048:2048 + user]
                parity = encode(clean, strength)
                # First parity byte always contains actual BCH bits, not padding.
                where = dict(main=1023, user=1024 + user - 1, parity=1024 + user)[region]
                parity += b'\xff' * (len(parity) % 2)
                code = bytearray(scramble(clean + parity, seed))
                code[where] ^= 0x80
                probe.run([request(PROGRAM, page=page, data=bytes(code).ljust(RAW, b'\xff'))])
                ret, regs, data = probe.run([request(CODEWORD, page=page,
                    length=1024, seed=seed, flags=9,
                    chunk=STRENGTHS.index(strength) | user << 8 | 1024 << 16)])[0]
                report['rows'].append(dict(strength=strength, user=user, region=region,
                    fault_byte=where, regs=regs[:5], result=ret,
                    main_matches=data[:1024] == clean[:1024],
                    user_matches=data[2048:2048 + user] == clean[1024:],
                    decoded_sha256=digest(data[:1024])))
            print('targeted main/user/parity errors', len(report['rows']), flush=True)
        elif a.phase == 'streams':
            # Program once, then vary column/seed/length; no repeated-page writes.
            raw = logical(0)
            probe.run([request(PROGRAM, data=raw)])
            specs, requests = [], []
            for seed in SEEDS:
                for column in (0, 1, 3, 4, 511, 512, 1023, 1024, 2047, 2048, 2112, 2167):
                    for flags in (1, 5, 33, 37):
                        length = min(73, RAW - column)
                        specs.append((seed, column, length, flags))
                        requests.append(request(READ, column=column, length=length,
                                                chunk=31, seed=seed, flags=flags))
            for start in range(0, len(requests), 256):
                results = probe.run(requests[start:start + 256])
                for (seed, col, length, flags), (ret, regs, data) in zip(specs[start:start + 256], results):
                    plain = raw[col:col + length]
                    expected = measured_stream(plain, seed, 31, True, bool(flags & 4))
                    if flags & 32:
                        key = bytes(x ^ y for x, y in zip(plain, expected)).translate(REVERSE)
                        expected = bytes(x ^ y for x, y in zip(plain, key))
                    report['rows'].append(dict(seed=seed, column=col, length=length, flags=flags,
                        matches=expected == data[:length], sha256=digest(data[:length])))
                print('stream cases', min(start + 256, len(requests)), flush=True)
            report['transitions'] = []
            for seed in (SEEDS[0], SEEDS[1], SEEDS[-1]):
                for chunk in (4, 31, 64, 513):
                    for flags in (65, 69):
                        length = min(4 * chunk, RAW)
                        _, regs, data = probe.run([request(TRANSITIONS, length=length, chunk=chunk,
                                                         seed=seed, flags=flags)])[0]
                        report['transitions'].append(dict(seed=seed, chunk=chunk, flags=flags,
                            data_hex=data[:length].hex(), regs=regs[:3]))
        elif a.phase == 'transitionwrites':
            for seed in (SEEDS[0], SEEDS[1], SEEDS[-1]):
                for chunk in (4, 31, 64, 513):
                    for flags in (65, 69):
                        page = 448 + len(report['rows'])
                        length = 4 * chunk
                        data = logical(0)[:length]
                        probe.run([request(PROGRAM, page=page, length=length,
                            chunk=chunk, seed=seed, flags=flags, data=data)])
                        raw = probe.run([request(READ, page=page)])[0][2]
                        report['rows'].append(dict(seed=seed, chunk=chunk, flags=flags,
                            data_hex=raw[:length].hex(), tail_erased=raw[length:] == b'\xff' * (RAW-length)))
            print('randomizer write toggles', len(report['rows']), flush=True)
        elif a.phase == 'callers':
            # Every write is to a freshly erased, different page.
            writes = [(NWRITE, 0, 0, 2048), (NWRITE, 1, 0, 2048),
                      (NSUBWRITE, 0, 0, 1024), (NSUBWRITE, 0, 1024, 1024),
                      (NOOBWRITE, 1, 0, 2048), (NRAWWRITE, 1, 0, 2048)]
            for index, (op, mode, col, length) in enumerate(writes):
                page, data = 448 + index, logical(index)
                wr = checked_native(probe, native(op, page, mode, col, length, data))
                row = dict(op=op, page_index=index, counts=wr[1][10:14], reads=[])
                raw = probe.run([request(READ, page=page)])[0][2]
                nr = checked_native(probe, native(NRAWREAD, page))
                row['raw_callback_matches'] = nr[2] == raw
                if op != NRAWWRITE:
                    for readmode in (0, 1):
                        for offset, size in ((0, 1), (1, 1023), (511, 514), (1023, 2), (1024, 1), (2047, 1), (0, 2048)):
                            ret, regs, read = checked_native(probe, native(NSUBREAD, page, readmode, offset, size))
                            full = checked_native(probe, native(NREAD, page, readmode))
                            first = offset // 1024 * 1024 if readmode == 0 else 0
                            last = ((offset + size + 1023) // 1024) * 1024
                            row['reads'].append(dict(mode=readmode, offset=offset, length=size,
                                result=ret, regs=regs, matches=read[offset:offset + size] == full[2][offset:offset + size],
                                untouched_prefix=read[:first] == b'\xa5' * first,
                                untouched_suffix=read[last:2048] == b'\xa5' * (2048 - last)))
                    oob = checked_native(probe, native(NOOBREAD, page))
                    full = checked_native(probe, native(NREAD, page))
                    row['oob_callback_matches'] = oob[2][2048:] == full[2][2048:]
                else:
                    row['raw_write_matches'] = raw == data
                report['rows'].append(row)
                print('caller', index, flush=True)
        elif a.phase == 'subpolicy':
            for index, kind in enumerate(('valid', 'erased', 'zero', 'erased-first', 'erased-second')):
                page, data = 448 + index, logical(index)
                raw = bytearray(page_image(data, page))
                if kind == 'erased':
                    raw[:] = b'\xff' * RAW
                elif kind == 'zero':
                    raw[:] = b'\0' * RAW
                elif kind == 'erased-first':
                    raw[:1024], raw[2048:2112] = b'\xff' * 1024, b'\xff' * 64
                elif kind == 'erased-second':
                    raw[1024:2048], raw[2112:2168] = b'\xff' * 1024, b'\xff' * 56
                probe.run([request(PROGRAM, page=page, data=raw)])
                full = checked_native(probe, native(NREAD, page, 2))
                row = dict(kind=kind, full_regs=full[1], full_sha256=digest(full[2][:2048]), reads=[])
                for col, length in ((0, 1), (1, 1023), (511, 514), (1023, 2), (1024, 1), (2047, 1)):
                    ret, regs, read = checked_native(probe, native(21, page, 2, col, length))
                    public = probe.run([request(22, page=page, column=col, length=length,
                        chunk=0, flags=0, data=b'\xa5' * RAW)], accept_errors=True)[0]
                    row['reads'].append(dict(offset=col, length=length, result=ret, regs=regs,
                        matches=read[col:col + length] == full[2][col:col + length],
                        public_result=public[0], public_regs=public[1][:5],
                        public_matches=public[2][:length] == full[2][col:col + length]))
                report['rows'].append(row)
                print('actual subpage policy', kind, flush=True)
        elif a.phase in ('mtd', 'mtdoob'):
            for index, (mode, length, ooblen) in enumerate(((0, 2048, 128), (1, 2048, 6),
                    (2, 2048, 128), (0, 0, 128), (1, 0, 6), (0, 2048, 0), (0, 1024, 0))):
                page, data = 448 + index, logical(index)
                written = probe.run([request(23, page=page, length=length, chunk=ooblen,
                                             flags=mode, data=data)], accept_errors=True)[0]
                row = dict(mode=mode, length=length, ooblen=ooblen, write_result=written[0], reads=[])
                if written[0] >= 0:
                    for offset, size, olen in ((0, length, ooblen), (1, 1, 0), (511, 514, 0),
                                               (1023, 2, 0), (2047, 1, 0), (0, 0, ooblen)):
                        if size == 0 and olen == 0:
                            continue
                        ret, regs, read = probe.run([request(22, page=page, column=offset,
                            length=size, chunk=olen, flags=mode, data=b'\xa5' * RAW)], accept_errors=True)[0]
                        main = (data[:length] + b'\xff' * (2048 - length))[offset:offset + size]
                        if not olen:
                            oob_matches = None  # No OOB bytes were requested.
                        elif mode in (1, 2):
                            oob_matches = read[2048:2048 + olen] == data[2048:2048 + olen]
                        else:
                            # PLACE overwrites parity on write, but preserves user/tail.
                            oob_matches = (read[2048:2056] == data[2048:2056] and
                                read[2168:2048 + olen] == data[2168:2048 + olen])
                        row['reads'].append(dict(offset=offset, length=size, ooblen=olen,
                            result=ret, regs=regs[:5], main_matches=read[:size] == main,
                            untouched_user_tail=read[size:2048] == b'\xa5' * (2048 - size),
                            oob_matches=oob_matches, oob_hex=read[2048:2048 + olen].hex()))
                report['rows'].append(row)
                print('MTD ioctls', index, written[0], flush=True)
        elif a.phase == 'recovery':
            data = logical(0)
            checked_native(probe, native(NWRITE, data=data))
            for repeat in range(3):
                for mode in (0, 1, 2):
                    for delay in (0, 1, 5, 10, 50, 100, 500, 5000):
                        ret, regs, read = probe.run([request(20, chunk=delay, seed=SEEDS[0], flags=mode)])[0]
                        after = checked_native(probe, native(NREAD))
                        report['rows'].append(dict(repeat=repeat, mode=mode, delay_us=delay,
                            result=ret, regs=regs[:13], completed_data_matches=read[:2048] == data[:2048],
                            subsequent_read_matches=after[2][:2048] == data[:2048],
                            subsequent_read_regs=after[1]))
                print('recovery cycle', repeat + 1, flush=True)
        else:
            data = logical(0)
            checked_native(probe, native(NWRITE, data=data))
            for batch in range(20):
                results = probe.run([native(NREAD, mode=i % 2) for i in range(100)])
                report['rows'].append(dict(batch=batch, reads=len(results),
                    bad_data=sum(r[2][:2048] != data[:2048] for r in results),
                    bad_guards=sum(r[1][14:] != [1, 16] for r in results),
                    fallback=sum(bool(r[1][10]) for i, r in enumerate(results) if i % 2),
                    failed_ecc=sum(r[1][1] for r in results)))
                print('stress reads', (batch + 1) * 100, flush=True)
    finally:
        path.write_text(json.dumps(report, indent=2) + '\n')
        restore(probe, directory)
        report['restored'] = True
        report['elapsed_seconds'] = time.monotonic() - started
        path.write_text(json.dumps(report, indent=2) + '\n')
    print('Restored original block; results:', path)


if __name__ == '__main__':
    main()
