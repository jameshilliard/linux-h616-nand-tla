# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent byte and uncertainty-boundary checks; no physical board access."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from codec_oracle import scramble
from randomizer_controller import (Controller, command, concrete, config,
                                   measured_profile, profile)
from run_randomizer import seed_tables


def resolved_old(**overrides):
    choices = dict(seedWrite='keep', modeWrite='keep', sameWrite='keep',
                   enableWrite='keep', select='keep', nandReset='keep',
                   controllerReset='keep', abort='keep', normalBlockRead=False,
                   normalBlockWrite=False, normalPageRead=False,
                   normalPageWrite=False, pageBoundary=False, readTailBypass=False,
                   eccStart=True, blockBoundary=True)
    choices.update(overrides)
    return concrete(profile('a33'), **choices)


class ControllerChecks(unittest.TestCase):
    def test_profiles_require_resolution(self):
        with self.assertRaises(ValueError):
            profile('unverified-soc')
        with self.assertRaises(ValueError):
            concrete(profile('a10'))
        with self.assertRaises(ValueError):
            Controller(profile('h616'))
        for soc in ('a10', 'a20', 'a23', 'a33'):
            self.assertEqual(profile(soc)['enableBit'], 9)
            self.assertEqual(profile(soc)['normalBlockRead'], {True, False})
        self.assertEqual(profile('h616')['enableBit'], 5)

    def test_programmed_seed_is_not_active_state(self):
        nfc = Controller(resolved_old())
        nfc.write(config(0x2b75, legacy=True, page=True))
        first = nfc.transfer(command(bytes(4)))
        nfc.write(config(0x0bd0, legacy=True, page=True))
        second = nfc.transfer(command(bytes(4)))
        self.assertEqual(first + second, scramble(bytes(8), 0x2b75))
        self.assertNotEqual(second, scramble(bytes(4), 0x0bd0))

    def test_all_seed_codewords(self):
        # Oracle recurrence is numeric; the controller uses a shifting bit array.
        # These are mathematical contracts, not newly measured transfers.
        for seed in (*seed_tables()['PageSeeds'], 0, 0x7fff):
            for step in (512, 1024):
                size = step + 4 + 56
                data = bytes((i * 37 + i // 256) & 255 for i in range(size * 2))
                expected = scramble(data[:size], seed) + scramble(data[size:], seed)
                for read in (True, False):
                    nfc = Controller(resolved_old(), initial=0x7fff, valid=True)
                    nfc.write(config(seed, legacy=True))
                    self.assertEqual(nfc.transfer(command(data, kind='PAGE' if read else 'ECC',
                                                         read=read, boundaries=(size,))), expected)

    def test_bsp_signatures(self):
        manifest = json.loads((Path(__file__).resolve().parents[1] /
                               'reference/randomizer/source.json').read_text())
        seeds = seed_tables()['PageSeeds']
        for row in manifest['spare_signatures']:
            offset = row['main_bytes']
            encoded = scramble(bytes([255]) * (offset + 4), seeds[row['page']])
            self.assertEqual(encoded[offset:], bytes(row['decoded_ff']))

    def test_disabled_bytes_do_not_advance(self):
        nfc = Controller(measured_profile())
        nfc.write(config(0x2b75, page=True))
        first = nfc.transfer(command(bytes(4)))
        before = list(nfc.bits)
        nfc.write(config(0x2b75, page=True, enabled=False))
        self.assertEqual(nfc.transfer(command(b'plain')), b'plain')
        self.assertEqual(nfc.bits, before)
        nfc.write(config(0x2b75, page=True))
        second = nfc.transfer(command(bytes(4)))
        self.assertEqual(first + second, scramble(bytes(8), 0x2b75))

    def test_read_tail_bypasses_but_advances(self):
        nfc = Controller(measured_profile())
        nfc.write(config(0x2b75, page=True))
        expected = scramble(bytes(9), 0x2b75)
        self.assertEqual(nfc.transfer(command(bytes(5))), expected[:4] + b'\0')
        self.assertEqual(nfc.transfer(command(bytes(4))), expected[5:])

    def test_msb_is_byte_bit_order(self):
        for policy, legacy in ((measured_profile(), False), (resolved_old(), True)):
            nfc = Controller(policy)
            nfc.write(config(0x2b75, legacy=legacy, msb=True))
            expected = bytes(int(f'{v:08b}'[::-1], 2) for v in scramble(bytes(8), 0x2b75))
            self.assertEqual(nfc.transfer(command(bytes(8), kind='ECC')), expected)

    def test_reset_and_abort_need_not_rewind(self):
        for reset in ('select', 'nand-reset', 'soft-reset', 'bus-reset'):
            for prefix in range(9):
                nfc = Controller(resolved_old())
                nfc.write(config(0x2b75, legacy=True))
                nfc.transfer(command(bytes(8), kind='ECC'), abort=prefix)
                active = list(nfc.bits)
                nfc.reset(reset, 1)
                self.assertEqual(nfc.bits, active)
                nfc.write(config(0x76db, legacy=True))
                self.assertEqual(nfc.transfer(command(bytes(8), kind='PAGE')),
                                 scramble(bytes(8), 0x76db))


if __name__ == '__main__':
    unittest.main()
