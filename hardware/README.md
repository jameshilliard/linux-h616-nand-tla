# Restricted H616 hardware diagnostic

For the current streaming-buffer recovery implementation, see its
[implementation and hardware record](../docs/H616-STREAMING-IMPLEMENTATION.md).
`install_bridge.py --abort-recovery` supports both the historical private-buffer
candidate and the current streaming candidate. On the latter, modes 1 and 2
both exercise the existing streaming path; no private buffer pointer exists
to override. Keep each capture tied to its original uninstrumented source hash.

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

## Post-abort data-path isolation

The [follow-up report](../docs/POST-ABORT-DATAPATH.md) documents the scope and
limits of the subsequent reset experiments. The same backup, RAM-only boot,
UBI exclusion and restore requirements apply. These matrices write only the
authorized block and issue only read-side aborts:

```sh
python3 hardware/run_reset_diagnosis.py --ram-only-confirmed \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
python3 hardware/run_reset_diagnosis.py --ram-only-confirmed --followup-matrix --repeat 6 \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
python3 hardware/run_reset_diagnosis.py --ram-only-confirmed --bus-reset-matrix --repeat 6 \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
```

Use the updated probe and bridge together. `--followup-matrix` adds restricted
SRAM inspection/fill and a single NORMAL read. `--bus-reset-matrix` additionally
requires the diagnostic kernel's `sunxi_nfc_lab_bus_reset` helper, which uses
the driver's owned reset control. No arbitrary MMIO or CCU access is exposed.
The helper is test-only and is not a proposed production error path.

After final restoration and unload with `finish_extended.py`, use
`export_reset.py` to export only these synthetic matrices. It never reads
original flash dumps. `run_hardware.py` replays the captured bytes offline
and deliberately preserves the observed soft-reset failures.

## Actual-driver abort recovery

The [candidate recovery report](../docs/H616-ABORT-RECOVERY.md) uses a newer
driver and the actual DMA-read timeout path rather than a separate diagnostic
reset sequence. Preserve all backup, RAM-only, UBI-detachment and block-7
authorization requirements above. Use a **fresh private evidence directory**;
do not reuse another run's NAND backups.

Build the kernel and module together after instrumenting the candidate:

```sh
python3 hardware/install_bridge.py /path/to/kernel-build --abort-recovery \
    --expected-source-sha256 7f93090564302e65e251c48d7a700ec4d20b1b46e2a29d4f777e747f638348cc
```

After independent boot and fresh backup verification, load the module with
both `allow_write=1` and `allow_recovery=1`. The write permission is needed
to prepare synthetic pages and restore the scratch block; only reads are
interrupted. The new operation arms one timeout after the real MDMA launch.

```sh
python3 hardware/run_native_abort.py --ram-only-confirmed --repeat 6 \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
python3 hardware/run_extended.py --ram-only-confirmed --phase stress \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
python3 hardware/finish_extended.py --directory /path/to/private-evidence \
    ssh root@TEST_BOARD
```

A driver-recovery failure poisons the diagnostic module. Do not force unload
or continue to access a controller held in reset; restore from independent
boot after restoring hardware health. On successful testing, preserve images
and source privately, remove the hooks with `install_bridge.py --remove` and
the matching uninstrumented source hash, rebuild the ordinary integration
kernel and return the board to its normal boot. A source directory newer than
the build copy must be resynchronized explicitly if the build uses rsync's
update-only policy.

`export_native_abort.py` selects only synthetic fields and requires the exact
192-trial matrix, 2,000 stress reads and final restoration checks. Review its
output before publication. The recorded capture uses the pre-indentation
hash documented in the report; a new source revision needs its own provenance
and immutable capture, not a silent replacement of this run.

## Read-only direct-buffer investigation

Operations 28/29 and [their report](../docs/H616-DIRECT-DMA.md) examine whether
the private coherent data buffer can be replaced by streaming mappings.
Use an independently booted RAM image, UBI detached, `allow_recovery=1`, and
**`allow_write=0`**. Preserve fresh off-board raw backups of all four SPL
copies. Operation 29 additionally admits read-only access to physical pages
2688-2751 on this fixed test geometry: the first block of the existing UBI
partition. Back it up with OOB as `nfc-ubi-block.before` (64 * 2176 bytes).
No erase/program command is granted access to this extended range. Do not
reuse that range blindly on another board or partition layout.

Install the current `--abort-recovery` bridge and rebuild the diagnostic
kernel/module together. The two runners issue only read/test requests:

```sh
python3 hardware/run_direct_retire.py --ram-only-confirmed --repeat 8 \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
python3 hardware/run_native_direct.py --ram-only-confirmed --repeat 8 \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
```

The first runner has 392 raw trials, including eight deliberate writes to
the *test RAM* poison buffer as detector controls. The second compares PIO,
private-buffer DMA and direct streaming DMA, then performs 128 injected
read aborts. The bridge temporarily bypasses the private buffer only inside
locked diagnostic reads. It retains allocations and poisons further access
if stop/ownership verification fails. Never force-unload a poisoned module.

After success, compare fresh independent raw dumps of all protected ranges
with their pre-test backups, unload the module, check ordinary MTD reads and
return to the normal image. Record these checks in private `direct-final.json`.
`export_direct.py` allowlists public fields and refuses incomplete matrices
or restoration checks. It exports no flash bytes, their hashes, transport
commands or private file paths. Review the export before publication.

`run_direct_ecc_stress.py` repeats the native matrix's isolated corrected-ECC
profile: 100 rounds of PIO, private DMA, direct DMA and a 5-us direct-DMA
abort. It has the same read-only/RAM-only preconditions and validates the
existing PIO/DMA parity-representation difference with the bit-level oracle.
The original observation remains in the capture even when repetitions pass.

## Current-source write aborts and PIO timeout reuse

Operations 30/31 and [their report](../docs/H616-REQUEST-FAULTS-20261105.md)
extend the actual-driver bridge. Operation 30 interrupts a DMA write before
the driver's explicit PROGRAM CONFIRM; operation 31 expires a PIO wait while
preserving its real timeout cleanup and observes the first following read.
They do not simulate an indefinitely stuck DMA engine or PIO state machine.

Use the matching **disposable build copy**, kernel and module. These hooks
require the abort-recovery candidate; they reject the pinned older driver.
The supported current uninstrumented source SHA-256 is shown below:

```sh
python3 hardware/install_bridge.py /path/to/kernel-build \
    --abort-recovery --request-faults \
    --expected-source-sha256 102b942b18d37e37f6f6dd605233da79149e2fbff61f38daf25acde75f0ce676
```

Rebuild and RAM-boot the diagnostic image without installing it in persistent
storage. These runners require the `rdinit=/init-lab` marker, no flash mounts,
and UBI explicitly detached. Initially load the probe with `allow_write=0`
and `allow_recovery=1`. Acquire a new private backup set:

```sh
python3 -m hardware.snapshot_requests --directory /path/to/private-evidence \
    ssh root@TEST_BOARD
```

The snapshot runner compares raw MTD reads of all four SPL partitions against
independent 512- and 1024-byte module reads and backs up the bounded UBI block.
Any mismatch stops the preflight; investigate it before authorizing writes.
Do not modify a mismatching dump to make it pass. Keep rejected observations
and obtain a fresh consistent backup set. No backup belongs in this repository.

Only after verification, reload with both `allow_write=1` and
`allow_recovery=1`. The next runner **erases and programs authorized block 7**:

```sh
python3 -m hardware.run_request_faults --ram-only-confirmed --repeat 4 \
    --directory /path/to/private-evidence ssh root@TEST_BOARD /tmp/probe_stdio
python3 -m hardware.snapshot_requests --verify \
    --directory /path/to/private-evidence ssh root@TEST_BOARD
```

The write matrix checks erased-page preservation, the actual DMA source,
explicit confirm count, subsequent normal writes and independent raw/ECC
readback. It holds the source allocation for 100 ms after successful recovery;
unchanged source memory does not detect late device reads. The PIO matrix
varies NORMAL/ECC-special command, delay, PIO/DMA follower and inter-request
gap. No diagnostic reset or discarded read is inserted between callbacks.

The runner attempts restoration in `finally`, unless a recovery failure has
poisoned the controller. A poisoned module must not be force-unloaded: retain
the private backups and restore from an independently booted healthy system.
No live flash consumers may run concurrently, and no array program/erase
after confirm or power interruption is part of this test.

After all raw backups match, unload the module, archive the diagnostic image
and remove the hooks with `install_bridge.py --remove` and the same source
hash. Rebuild and RAM-boot the ordinary integration Image, without the module,
and detach UBI. Then run three non-destructive 64 MiB MTD reads and verify the
scratch block again:

```sh
python3 -m hardware.finish_requests --directory /path/to/private-evidence \
    ssh root@TEST_BOARD
```

Restore normal boot, check that UBI attaches normally, then finalize and export:

```sh
python3 -m hardware.finish_requests --normal-boot \
    --directory /path/to/private-evidence ssh root@TEST_BOARD
python3 -m hardware.request_faults_evidence \
    --export-directory /path/to/private-evidence --capture /path/to/new-capture.json
python3 run_hardware.py
```

The exporter allowlists numeric observations and comparison booleans and
refuses to replace a capture. It requires the complete four-repeat matrix,
real timeout/late-event witnesses and final restoration checks. Inspect the
export before publishing. Replaying a capture verifies its recorded assertions;
it does not extend the hardware evidence to unmeasured failure modes.
