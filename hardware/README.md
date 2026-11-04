# Restricted H616 hardware diagnostic

This is a dangerous board-specific diagnostic, not a general MTD utility or
upstream driver patch. Read the [measurement report](../docs/HARDWARE-MEASUREMENTS-20261104.md).

The module requires H616, 256 MiB NAND, 2 KiB+128 pages, 128 KiB eraseblocks,
BCH-32/1024 and an `spl-3` partition at 0xc0000 of size 0x40000. Reads cover
the four SPL copies; writes are limited to physical block 7. These addresses
are test-board constraints, **not** a universal Allwinner partition layout.
Do not use it unless that block is explicitly authorized expendable backup
storage and independent recovery boot is available. Do not unbind NAND or
inject arbitrary DMA faults. The optional read-side recovery experiments below
require a RAM-only environment and retain their DMA allocations.

Build against the matching running kernel and toolchain:

```sh
make -C /path/to/kernel-build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    M="$PWD/hardware" modules
make -C /path/to/kernel-build ARCH=arm64 headers_install \
    INSTALL_HDR_PATH=/path/to/private-output/uapi
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -static \
    -I/path/to/private-output/uapi/include hardware/probe_stdio.c \
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
Do not use Python's `-O` or `PYTHONOPTIMIZE`; the harness rejects optimization
because it would disable validation assertions. All independent backup files
are checked again before any destructive phase begins.

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

## Extended native-path tests

Read the [extended report](../docs/HARDWARE-EXTENDED-20261104.md), including the
unresolved reset finding, before reproducing these experiments. They use a
diagnostic kernel built from the normal integration branch. The userspace MTD
operations require sufficiently recent UAPI headers for `MEMREAD`.

Install the bridge **only in a disposable, already-synchronized build tree**:

```sh
python3 hardware/install_bridge.py /path/to/kernel-build \
    --expected-source-sha256 f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6
```

Rebuild that kernel and the module together and boot the diagnostic image from
independent recovery media into RAM. Do not install this image in persistent
production storage. Stop flash consumers and detach UBI; checking that there
are no UBIFS mounts alone is insufficient. The module independently rejects
recovery operations while a UBI device is attached. This is a restricted lab
precondition, not protection against another administrator attaching UBI while
the test runs.

After fresh backup verification, use `allow_write=1`. Each phase erases the
authorized scratch block and restores it, even the matrices whose observations
are reads. Do not run phases concurrently or interrupt restoration:

```sh
python3 hardware/run_extended.py --ram-only-confirmed --phase native \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
```

Other phases are `context`, `codewords`, `eccregions`, `streams`,
`transitionwrites`, `callers`, `subpolicy`, `stress`, `mtdoob` and `recovery`.
`mtd` preserves the earlier diagnostic matrix; use `mtdoob` for the corrected
OOB comparisons. Public MTD writes require an additional explicit transport
option, `/tmp/probe_stdio --allow-mtd-write`; all those writes independently
check the partition name, offset and geometry and remain within block 7.

`callers` deliberately invokes generic subpage helpers that the vendor layout
does not install; its partial-write experiments bypass a public restriction.
Treat the `subpolicy` and public-MTD results as the reachable-caller check,
not every mismatch from direct unsupported helper combinations as a kernel bug.

`memory_load.c` is an optional three-process RAM-only fill/verify load. Run it
concurrently with `stress`; it does not access flash or other devices.

Recovery requires **both** `allow_recovery=1` on the module and the RAM-only
precondition. The retained coherent data/descriptor buffers outlive every reset
and observation window. A reset timeout poisons/pins the module: do not try to
unload it or retry flash operations. Recover by power cycling into independent
recovery media, reestablish a healthy controller, then restore the private backup.
No program/erase command is aborted by these tests.

```sh
python3 hardware/run_abort_followup.py --ram-only-confirmed --variant-matrix \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
```

The variants include withholding IRQ delivery during actual driver reads, so
successful reads intentionally take longer while `wait_for_completion_timeout()`
expires and the driver's status recheck runs. This is not a genuinely hung
DMA-provider test. The capture also contains first reads corrupted after active
abort, including cases without failed-ECC accounting: do not generalize these
experiments into a production recovery recipe.

After every phase restored successfully, compare all four backups and unload:

```sh
python3 hardware/finish_extended.py --directory /path/to/private-evidence \
    ssh root@TEST_BOARD
```

Archive the test kernel/module and source privately before removing bridge
instrumentation. Verify the original source hash and rebuild the normal image:

```sh
python3 hardware/install_bridge.py /path/to/kernel-build --remove \
    --expected-source-sha256 f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6
```

`export_extended.py` has an explicit synthetic-report whitelist and requires
verified final restoration and module unload. It never reads the raw backup
files. Still inspect exported content before publishing; the privacy check is
not a general secret detector. The original capture is immutable: use a new
filename for a new run rather than replacing historical evidence.
