# Restricted H616 hardware diagnostic

This is a dangerous board-specific diagnostic, not a general MTD utility or
upstream driver patch. Read the [measurement report](../docs/HARDWARE-MEASUREMENTS-20261104.md).

The module requires H616, 256 MiB NAND, 2 KiB+128 pages, 128 KiB eraseblocks,
BCH-32/1024 and an `spl-3` partition at 0xc0000 of size 0x40000. Reads cover
the four SPL copies; writes are limited to physical block 7. These addresses
are test-board constraints, **not** a universal Allwinner partition layout.
Do not use it unless that block is explicitly authorized expendable backup
storage and independent recovery boot is available. Do not unbind NAND or
inject faults that may leave DMA running while it is loaded.

Build against the matching running kernel and toolchain:

```sh
make -C /path/to/kernel-build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    M="$PWD/hardware" modules
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -static hardware/probe_stdio.c \
    -o /path/to/private-output/probe_stdio
```

Copy `nfc_probe.ko` and `probe_stdio` to the target. The root-only misc device
also requires `CAP_SYS_RAWIO`; only the tested 64-bit ABI is supported. It
exposes no arbitrary register writes or arbitrary NAND commands.

Collect fresh raw backups on the target and load read-only:

```sh
for n in 0 1 2 3; do
    nanddump -q -n -o --bb=dumpbad -f /tmp/nfc-spl-$n.before /dev/mtd$n || exit
done
insmod /tmp/nfc_probe.ko
```

Copy all four files into a private **off-board** directory outside this repo:

```sh
python3 hardware/run_probe.py --directory /path/to/private-evidence \
    ssh root@TEST_BOARD /tmp/probe_stdio
```

This acquires independent 512/1024-command backups, compares them with the MTD
dumps, and refuses to replace a differing existing backup. The default phase
never erases or programs flash. Keep backups and private logs out of this repo.

Only after those checks and explicit authorization, reload the module with
`allow_write=1` and run:

```sh
python3 hardware/run_probe.py --phase write --directory /path/to/private-evidence \
    ssh root@TEST_BOARD /tmp/probe_stdio
```

This **erases/programs the designated backup block**. It attempts restoration
in `finally` and verifies every raw byte afterward, but cannot guarantee
restoration after power loss. Preserve backups and independent recovery boot.
`--phase restore` permits an explicit recovery attempt after reestablishing a
healthy controller; do not repeatedly retry a hung one. Finally compare all
four partitions using fresh raw dumps and unload the module.

Never restore using another run's NAND contents. The published JSON contains
measurements, not backups. `python3 run_hardware.py` accesses no board: it
replays synthetic hashes and register assertions and is safe for CI.
