# H616 retained-lifetime hardware validation, 2026-11-07

## Tested implementation

This run combines the sunxi NAND v11 draft with the posted raw-NAND retained
lifetime v1 common helpers and the posted mtdchar chunking fixes. The local
lifetime v2 draft has the same implementation as v1 at this point. The common
MTD/NAND/NVMEM lifetime files and sunxi driver in the hardware integration were
compared with the submission tree before testing.

The submission snapshot is `26d25a3d1707f81106f3371a76e3957bd5225bb0`;
the hardware integration snapshot is
`33d173e9cd42bbb4d8ce5bd47b5618bd8d24a112`. These identify local snapshots,
not merged upstream commits. The tested sunxi source SHA-256 is
`60f5efccbeaeda4f8ccefc3e07be396bb5557dac147310a6ff244a58e4aa81d3`.

Hardware is H616 with a Toshiba TC58NVG1S3H, 256 MiB SLC NAND, 2048+128-byte
pages and 128 KiB eraseblocks. Native storage uses the vendor randomized-OOB
format. Boot0 views expose 1024 data bytes and 1152 OOB bytes per logical page.
The test kernel enables generic KASAN, lockdep, kmemleak and debug-list/object
checks. The controller driver is built in; the NVMEM test consumer is a module.

## Results

| Check | Coverage | Result |
| --- | --- | --- |
| Baseline Boot0 reads | Four views; decoded image and complete physical main+OOB records | Matched saved images and native raw reads |
| Native reads | Two 64 MiB reads before stress, another 64 MiB read afterward | Matching initial hashes; final read completed in full |
| Retained file descriptors | 20 unbind/rebind cycles with concurrent native and Boot0 readers | 5,451 native and 700 Boot0 reads matched their baselines |
| Disconnected handles | `read`, `MEMREAD`, `MEMGETBADBLOCK`, before and after replacement probing | Returned `ENODEV`; `MEMGETINFO` and `ECCGETSTATS` remained accessible |
| Replacement ownership | Rebind while old handles remain; alternate native/Boot0 final-close order | Replacement devices had distinct indices and correct contents; old handles stayed disconnected |
| Retained NVMEM consumers | Five cycles holding both native and Boot0 providers across removal and reprobe | Initial reads succeeded; retained reads returned `ENODEV` after removal and after replacement; final puts completed |
| Boot0 writes | Plain `nandwrite`, plus all-FF, zero and patterned tail pages | Decoded readback matched; protected BootROM bytes matched host BCH/LFSR encoding for all 128 seed indices |
| Bulk `MEMREAD` | Raw and decoded reads of 1/2/3/31/63/64/65/112/128 pages | All 18 comparisons passed; no corrected or uncorrectable ECC counts |
| Native writes | 256 KiB pattern through the native vendor-format MTD path | Readback matched byte-for-byte |
| Debug diagnostics | Full test run, including final releases and subsequent I/O | No KASAN/lockdep warnings; both requested kmemleak scans were empty |
| Restoration | All SPL copies and secure storage raw; U-Boot decoded contents | Matched pre-test backups; normal NAND boot restored |

Only the previously designated secondary SPL range was erased/programmed.
It was restored from a complete physical main+OOB backup after each write test.
Other SPL copies, the U-Boot partition and secure storage were independently
compared afterward. Raw images, credentials, local paths, addresses and device
identifiers are deliberately not published.

The debug kernel's only final taint was the out-of-tree test module bit.
No performance claim is made from this heavily instrumented kernel.

## Reusable read-only fixtures

- [`h616_retained_fds.c`](../tests/kernel/retained_lifetime/h616_retained_fds.c)
  races real `pread()` operations with explicitly requested unbind/rebind,
  checks stale ioctls, then compares the replacement devices.
- [`h616_nvmem_hold.c`](../tests/kernel/retained_lifetime/h616_nvmem_hold.c)
  holds actual NVMEM consumer references. The `check` parameter accepts `N`
  for readable providers or `Y` for disconnected providers. It verifies both
  providers and releases their references on module removal.

Compile the userspace fixture with an AArch64 cross compiler using
`-O2 -Wall -Wextra -Werror -static -pthread`. Build the consumer module against
the exact running test kernel. The userspace arguments are driver name,
platform-device name, native MTD name, Boot0 MTD name and cycle count. Module
parameters `native_name` and `boot_name` are the NVMEM provider names (`mtdN`).

These fixtures must run from an isolated RAM environment with no attached UBI
devices or mounted MTD consumers. Unbinding the controller is disruptive even
though these two fixtures do not program flash. Do not run them against a
production root filesystem. The separate flash-write tests require verified
backups and an explicitly expendable region; they are not automated by these
fixtures.

## Test-environment findings and limits

The initial full firmware initramfs exceeded available RAM with all debugging
enabled. A small diagnostic rootfs allowed the same debug kernel to run. This
was an initramfs-unpacking OOM, not a retained-object test failure.

The installed downstream U-Boot partition fixup also interfered with the new
`boot-areas` DT subtree and reintroduced UBI auto-attachment. The successful
test used its volatile `skip_board_fixup=1` setting and checked the actual
running MTD layout and absence of attached UBI devices. This first run did not
validate the normal bootloader handoff; the follow-up below addresses that
integration separately.

This adds physical H616 coverage to the synthetic-controller and bounded-model
checks. It does not establish correctness for every interleaving, all other
drivers in the lifetime series, other NFC generations/geometries, controller
module unloading, actual hardware hot-removal, suspended-device removal,
allocation-failure unwind or power loss. Native read/write and Boot0 tests use
normal callbacks; explicit PIO/MDMA forcing and fault injection from earlier
hardware campaigns were not repeated here. Outstanding mailing-list review
items remain separate from these runtime results.

## Normal partition-fixup follow-up

The U-Boot fixup now resolves the native partition container before modifying
the tree and uses the same node for deletion and insertion. Explicit
`partitions` nodes take precedence over first-child traversal. The board's
UBI tagging is restricted to the native partition entries, leaving the
separate `boot-areas` subtree unchanged.

An extracted-code host harness exercises the actual fixup and board helpers
with libfdt, ASan and UBSan. All 16 layouts pass two successive fixups,
including both sibling orders, missing and empty native containers,
unrelated controller children and legacy direct-partition layouts. The
unchanged implementation fails 10 of these layouts. This is executable
regression coverage, not a new formal-model result.

The corrected U-Boot was booted from temporary SD media through the signed
SPL storage-reader path. The same diagnostic Linux image then booted with
the normal board fixup enabled. The actual handed-off DT retained all four
Boot0 entries and contained exactly seven native partitions. The native UBI
partition automatically attached at boot and after each controller reprobe.

After explicitly detaching UBI before each removal, another 20 retained-FD
cycles passed with 5,136 native reads and 659 Boot0 reads matching their
baselines. Five retained-NVMEM consumer cycles also passed. Old handles
remained disconnected after reprobe, metadata ioctls remained accessible,
and replacement devices returned the expected contents. There were no
KASAN/lockdep reports, and the requested kmemleak scan was empty. These FD
cycles ran separately to allow UBI detachment between them, so this follow-up
did not repeat the alternating final-close order covered by the first run.

All four Boot0 views again matched their decoded and raw baselines, a final
64 MiB native read completed, and the SPL, U-Boot and secure-storage contents
matched their backups. This follow-up performed no explicit NAND
erase/program operations; normal UBI attachment may perform flash bookkeeping.
The original SD boot area was restored and verified, and the board returned
to its original NAND firmware.

A separate pre-existing fixup issue remains: it emits `read_only`, while the
Linux fixed-partition parser expects `read-only`. No write was attempted to
test that protection, and the successful handoff checks do not establish
read-only enforcement. The other limits listed above still apply.
