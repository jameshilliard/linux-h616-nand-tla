#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Hardware measurements, deliberately separate from software emulation.

Use SSH keys or an externally supplied sshpass command. Never put passwords,
existing flash data, or private connection details in public result files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from codec_oracle import scramble, encode

RAW = 2176
HEADER = struct.Struct('<7Ii16I')
SIZE = HEADER.size + RAW
INFO, READ, COUNT, BANKS, ERASE, PROGRAM, ECC_READ, ECC_WRITE = range(8)
PIO_ECC_READ, EVENTS = 8, 9


def measured_stream(data, seed, chunk, read, page_mode=False):
    """Observed H616 normal commands, not a claim about older controllers."""
    keys = scramble(bytes(len(data)), seed)
    out = bytearray()
    for offset in range(0, len(data), chunk):
        part = data[offset:offset + chunk]
        start = offset if read and page_mode else 0
        key = bytearray(keys[start:start + len(part)])
        if read and len(part) % 4:
            key[-1] = 0
        out.extend(a ^ b for a, b in zip(part, key))
    return bytes(out)


def request(op, page=448, column=0, length=RAW, chunk=1024, seed=0,
            flags=0, data=b''):
    assert len(data) <= RAW
    return HEADER.pack(op, page, column, length, chunk, seed, flags,
                       0, *([0] * 16)) + data.ljust(RAW, b'\0')


class Probe:
    def __init__(self, command):
        self.command = command

    def run(self, requests, accept_errors=False):
        result = subprocess.run(self.command, input=b''.join(requests),
                                stdout=subprocess.PIPE, check=True, timeout=180)
        assert len(result.stdout) == len(requests) * SIZE
        out = []
        for i in range(len(requests)):
            response = result.stdout[i * SIZE:(i + 1) * SIZE]
            header = HEADER.unpack_from(response)
            if not accept_errors:
                assert header[7] >= 0, (header[:8], header[8:])
            out.append((header[7], list(header[8:]), response[HEADER.size:]))
        return out


def readonly(probe, directory):
    info = probe.run([request(INFO)])[0][1]
    assert info[:7] == [2048, 128, 131072, 1024, 32, 56, 2], info
    report = {'geometry': info[:7], 'initial_registers': info[7:], 'read_only': True}

    # Independent reads, different command boundaries, compare NAND raw ioctl.
    copies = {}
    for chunk in (512, 1024):
        responses = probe.run([request(READ, page=p, chunk=chunk)
                               for p in range(512)])
        copies[chunk] = b''.join(r[2] for r in responses)
        backup_file = directory / f'module-backup-{chunk}.raw'
        if backup_file.exists():
            assert backup_file.read_bytes() == copies[chunk], 'refusing to replace a different backup'
        else:
            backup_file.write_bytes(copies[chunk])
    assert copies[512] == copies[1024], 'chunked backups differ'
    existing = b''.join((directory / f'nfc-spl-{n}.before').read_bytes()
                        for n in range(4))
    assert existing == copies[512], 'MTD and module backups differ'
    report['backup_pages_verified'] = 512
    raw_page = copies[512][448 * RAW:449 * RAW]
    counts = []
    for count in (0, 1, 2, 63, 64, 255, 511, 512, 1023, 1024, 1025, 1536, 2048, 2049):
        _, registers, data = probe.run([request(COUNT, chunk=count)])[0]
        expected = count & 1023 or 1024
        match = data[:expected] == raw_page[:expected]
        sentinel = data[expected:1024] == bytes([0xa5]) * (1024 - expected)
        counts.append({'written': count, 'readback': registers[0],
                       'expected_length': expected, 'prefix_matches': match,
                       'tail_untouched': sentinel, 'status': registers[1]})
    report['counts'] = counts

    _, _, banks = probe.run([request(BANKS)])[0]
    report['independent_1k_sram_banks'] = (
        banks[:1024] == b''.join(struct.pack('<I', 0x13579bdf ^ i) for i in range(256))
        and banks[1024:2048] == b''.join(struct.pack('<I', 0xeca86420 ^ i) for i in range(256)))

    streams = []
    for page in (448, 449, 455):
        raw_page = copies[512][page * RAW:(page + 1) * RAW]
        for seed in (0x2b75, 0x0bd0, 0x7fff):
            for chunk in (1, 7, 63, 64, 511, 512, 1023, 1024):
                for flags in (1, 3, 5):
                    _, registers, data = probe.run([request(READ, page=page, chunk=chunk,
                                                           seed=seed, flags=flags)])[0]
                    continuous = scramble(raw_page, seed)
                    restarted = b''.join(scramble(raw_page[i:i + chunk], seed)
                                         for i in range(0, RAW, chunk))
                    streams.append({'page_in_block': page % 64, 'seed': seed,
                                    'chunk': chunk, 'flags': flags,
                                    'continuous': data == continuous,
                                    'restarted': data == restarted,
                                    'measured_contract': data == measured_stream(raw_page, seed, chunk, True, bool(flags & 4)),
                                    'keystream_sha256': hashlib.sha256(bytes(a ^ b for a, b in zip(raw_page, data))).hexdigest(),
                                    'ecc_ctl_after': registers[2]})
    report['randomizer_streams'] = streams
    tail = []
    raw_page = copies[512][448 * RAW:449 * RAW]
    for flags in (1, 9, 17, 25):
        for size in (1, 2, 3, 4, 5, 6, 7, 8, 31, 63, 64, 1023):
            _, _, data = probe.run([request(READ, length=size, chunk=size,
                                             seed=0x2b75, flags=flags)])[0]
            expected = scramble(raw_page[:size], 0x2b75)
            tail.append(dict(flags=flags, size=size,
                             differences=[i for i, (a, b) in enumerate(zip(data, expected)) if a != b]))
    events = probe.run([request(EVENTS)])[0][1][:4]
    (directory / 'tail-access-report.json').write_text(json.dumps(dict(rows=tail, events=events), indent=2) + '\n')
    (directory / 'readonly-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'randomizer_streams'}, indent=2))
    print('Randomizer matches:', {key: sum(row[key] for row in streams)
                                  for key in ('continuous', 'restarted')}, 'of', len(streams))


SEEDS = (0x2b75, 0x0bd0, 0x5ca3, 0x62d1, 0x1c93, 0x07e9, 0x2162, 0x3a72,
         0x0d67, 0x67f9, 0x1be7, 0x077d, 0x032f, 0x0dac, 0x2716, 0x2436,
         0x7922, 0x1510, 0x3860, 0x5287, 0x480f, 0x4252, 0x1789, 0x5a2d)


def page_image(logical, page):
    """Vendor packed OOB: 8 protected bytes in step zero, none in step one."""
    out = bytearray(RAW)
    seed = SEEDS[page % 64]
    offset = 2048
    for step, user in enumerate((8, 0)):
        data = logical[1024 * step:1024 * (step + 1)] + logical[offset:offset + user]
        codeword = scramble(data + encode(data, 32), seed)
        out[1024 * step:1024 * (step + 1)] = codeword[:1024]
        out[offset:offset + user + 56] = codeword[1024:]
        offset += user + 56
    out[offset:] = scramble(logical[offset:], seed)
    return bytes(out)


def restore(probe, directory):
    full = (directory / 'module-backup-512.raw').read_bytes()
    assert full == (directory / 'module-backup-1024.raw').read_bytes()
    assert len(full) == 512 * RAW
    assert full[384 * RAW:] == (directory / 'nfc-spl-3.before').read_bytes()
    backup = full[448 * RAW:512 * RAW]
    assert len(backup) == 64 * RAW
    probe.run([request(ERASE)])
    for i in range(64):
        page = backup[i * RAW:(i + 1) * RAW]
        if page != b'\xff' * RAW:
            probe.run([request(PROGRAM, page=448 + i, data=page)])
    verify = b''.join(r[2] for r in probe.run([request(READ, page=p, chunk=512)
                                            for p in range(448, 512)]))
    assert verify == backup, 'RESTORE FAILED: preserve backups and use recovery boot'
    (directory / 'scratch-restored.txt').write_text('Full raw data + OOB restoration verified.\n')


def destructive(probe, directory):
    backup = (directory / 'module-backup-512.raw').read_bytes()
    assert backup == (directory / 'module-backup-1024.raw').read_bytes()
    assert len(backup) == 512 * RAW
    scratch = b''.join(r[2] for r in probe.run([request(READ, page=p)
                                              for p in range(448, 512)]))
    assert scratch == backup[448 * RAW:], 'scratch changed since verified backup'
    # Prove the writable module still rejects targets outside the authorized block.
    for op in (ERASE, PROGRAM, ECC_WRITE):
        assert probe.run([request(op, page=0)], accept_errors=True)[0][0] == -30
    report = {'scratch_block': 7, 'native_ecc': [], 'software_codewords': [],
              'raw_stream_writes': [], 'restored': False}
    try:
        probe.run([request(ERASE)])
        erased = probe.run([request(READ, page=p) for p in range(448, 512)])
        assert all(r[2] == b'\xff' * RAW for r in erased)
        empty = probe.run([request(ECC_READ)])[0]
        report['erased_read'] = {'return': empty[0], 'regs': empty[1],
                                 'all_ff': empty[2] == b'\xff' * RAW}
        for index in range(8):
            page = 448 + index
            logical = bytes((i * 37 + (i >> 8) * 13 + index * 7) & 255 for i in range(RAW))
            probe.run([request(ECC_WRITE, page=page, data=logical)])
            actual = probe.run([request(READ, page=page)])[0][2]
            expected = page_image(logical, page)
            result, regs, decoded = probe.run([request(ECC_READ, page=page)])[0]
            pio = probe.run([request(PIO_ECC_READ, page=page, column=step * 1024,
                                      length=1024, seed=SEEDS[page % 64]) for step in (0, 1)])
            diffs = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
            protected_ok = decoded[:2048] == logical[:2048] and decoded[2048:2056] == logical[2048:2056]
            report['native_ecc'].append({'index': index, 'return': result, 'regs': regs,
                                         'physical_matches_oracle': not diffs,
                                         'first_differences': diffs[:10],
                                         'protected_matches': protected_ok,
                                         'tail_matches': decoded[-8:] == logical[-8:],
                                         'physical_sha256': hashlib.sha256(actual).hexdigest()})
            report['native_ecc'][-1]['pio'] = [dict(regs=row[1][:5],
                data_matches=row[2][:1024] == logical[step * 1024:(step + 1) * 1024],
                user_matches=step == 1 or row[2][2048:2056] == logical[2048:2056])
                for step, row in enumerate(pio)]
            (directory / f'synthetic-native-{index}.raw').write_bytes(actual)
            print('native', index, report['native_ecc'][-1], flush=True)
        # Program independent codewords, never using the hardware encoder's output.
        for index, (region, errors) in enumerate(((0, 0), (0, 1), (0, 16), (0, 32),
                                                (0, 65), (2048, 1), (2048, 32),
                                                (2056, 16), (2056, 32), (1024, 32),
                                                (2112, 32), (2170, 1))):
            page = 456 + index
            logical = bytes((i * 29 + index * 11 + 3) & 255 for i in range(RAW))
            physical = bytearray(page_image(logical, page))
            for bit in range(errors):
                physical[region + bit // 8] ^= 1 << (bit % 8)
            probe.run([request(PROGRAM, page=page, data=physical)])
            assert probe.run([request(READ, page=page)])[0][2] == physical
            result, regs, decoded = probe.run([request(ECC_READ, page=page)])[0]
            pio = probe.run([request(PIO_ECC_READ, page=page, column=step * 1024,
                                      length=1024, seed=SEEDS[page % 64]) for step in (0, 1)])
            protected_ok = decoded[:2048] == logical[:2048] and decoded[2048:2056] == logical[2048:2056]
            report['software_codewords'].append({'index': index, 'region': region,
                'injected_bitflips': errors, 'return': result, 'regs': regs,
                'protected_matches': protected_ok, 'tail_matches': decoded[-8:] == logical[-8:]})
            report['software_codewords'][-1]['pio'] = [dict(regs=row[1][:5],
                data_matches=row[2][:1024] == logical[step * 1024:(step + 1) * 1024],
                user_matches=step == 1 or row[2][2048:2056] == logical[2048:2056])
                for step, row in enumerate(pio)]
            print('software', index, report['software_codewords'][-1], flush=True)
        index = 0
        for flags in (1, 5):
            for chunk in (1, 4, 7, 64, 511, 512, 1023, 1024):
                page = 468 + index
                logical = bytes((i * 19 + index * 31) & 255 for i in range(RAW))
                probe.run([request(PROGRAM, page=page, data=logical,
                                   flags=flags, seed=0x2b75, chunk=chunk)])
                physical = probe.run([request(READ, page=page)])[0][2]
                decoded = probe.run([request(READ, page=page, flags=flags,
                                             seed=0x2b75, chunk=chunk)])[0][2]
                continuous = scramble(logical, 0x2b75)
                restarted = b''.join(scramble(logical[i:i + chunk], 0x2b75)
                                     for i in range(0, RAW, chunk))
                report['raw_stream_writes'].append({'flags': flags, 'chunk': chunk,
                    'continuous': physical == continuous, 'restarted': physical == restarted,
                    'roundtrip': decoded == logical,
                    'measured_write_contract': physical == measured_stream(logical, 0x2b75, chunk, False, bool(flags & 4)),
                    'measured_read_contract': decoded == measured_stream(physical, 0x2b75, chunk, True, bool(flags & 4)),
                    'physical_sha256': hashlib.sha256(physical).hexdigest()})
                (directory / f'synthetic-stream-{index}.raw').write_bytes(physical)
                print('stream', index, report['raw_stream_writes'][-1], flush=True)
                index += 1
    finally:
        (directory / 'write-report.json').write_text(json.dumps(report, indent=2) + '\n')
        restore(probe, directory)
        report['restored'] = True
        (directory / 'write-report.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True,
                        help='PRIVATE evidence directory, outside this repository')
    parser.add_argument('--phase', choices=('readonly', 'write', 'restore'), default='readonly')
    parser.add_argument('command', nargs=argparse.REMAINDER,
                        help='transport command, e.g. ssh root@board /tmp/probe_stdio')
    args = parser.parse_args()
    assert args.directory.is_dir()
    root = Path(__file__).resolve().parent.parent
    assert not args.directory.resolve().is_relative_to(root), 'private data must stay out of repo'
    {'readonly': readonly, 'write': destructive, 'restore': restore}[args.phase](Probe(args.command), args.directory)


if __name__ == '__main__':
    main()
