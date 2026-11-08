# Sunxi NAND prerequisite review corrections

## Snapshots and separation

These are local draft identifiers, not merged or newly posted prerequisites:

| Component | Snapshot | Scope |
| --- | --- | --- |
| mtdchar v2 | `6c4f993ed125` | Four patches, including completion of OOB after data exhaustion |
| Retained NAND lifetime v2 | `715ffbda487a` | Review corrections folded into the existing 58 patches |
| Sunxi v11 | `d657df3765d2` | 29 patches; BootROM interface documentation added |
| Existing-driver prefix | `12f0c4222edf` | First 24 sunxi patches on NAND-next `30daaf3e7897`, without generic prerequisites |
| Combined preparation | `f954ebb18c80` | Both corrected local prerequisites plus the 29 sunxi patches |

The combined preparation and submission have identical sunxi driver bytes:
`60f5efccbeaeda4f8ccefc3e07be396bb5557dac147310a6ff244a58e4aa81d3`.
The independent prefix preserves the base's original allocation/cleanup
mechanism; its scan-before-publication patch uses ordinary `nand_cleanup()`.
It is not claimed to solve retained-handle lifetime by itself.

The submission still names the posted lifetime v1 and mtdchar v1. Its cover
explicitly identifies the pending prerequisite respins. No local-only patch
ID is represented as a posted mailing-list dependency. Sent changelogs and
the author's sign-offs are retained.

## Lifetime corrections

- NVMEM owns a separate reference to the context-callback module and drops
  it after `priv_put()` returns. The callback's module need not be the
  provider's consumer owner.
- MTD get/put uses the acquired object and build configuration rather than
  a mutable master device type. A later master registration cannot change
  the meaning of a retained partition reference.
- Dynamic partition publication is serialized against master removal;
  early registration failure clears the assigned device type.
- BBT recovery obtains the NAND operation locks before writing a marker.
  A disconnected operation exits instead of treating `ENODEV` as media wear.
- Suspend of a retained disconnected NAND is a successful no-op.
- R852 handles card-event status during removal but stops queuing new work.
- Cleanup-variable initialization and the Orion/MPC5121 introducing-commit
  trailers are corrected in their existing patches.

Pre-existing virtual-concat unwind and hardware-specific teardown findings
remain independent fixes. This update does not claim to close those separate
reviews or validate every converted controller.

## Evidence

| Layer | Result |
| --- | --- |
| mtdchar extracted C | 5,771 transfers with no mismatch; four focused v1 failures reproduced |
| mtdchar bounded protocol | All 17 expected outcomes, including mutation/assumption controls |
| mtdchar C-to-TLA traces | All nine expected outcomes |
| Retained lifetime extracted C | 129 schedules and 242 successful file opens/closes; ASan/UBSan and separate coverage runs pass |
| Historical lifetime pin | Same host schedule suite still passes independently |
| QEMU synthetic MTD | Retained master, partition and nested handles pass; six backing releases |
| QEMU synthetic raw NAND | Unbind/rebind passes; four chips and two controllers released |
| QEMU OTP | Retained consumers, device references and failure unwind pass; nine backing releases |
| QEMU review regressions | Stable master-reference accounting, rejection of post-unregister partition creation, callback-module pin held inside `priv_put()` and released afterward |
| Kernel diagnostics | KASAN and lockdep enabled; no reports in the QEMU run; NAND and MTD modules unload afterward |
| Independent sunxi prefix | 1,272 lifecycle/probe cases, including 173 publication/unwind cases and 28 availability cases |
| Combined BootROM source | Actual transfer and erase bodies pass the bounded memory-backend fixture |
| Build/style | Updated generic and sunxi objects plus Denali/Atmel compile with `W=1`; correction diff passes strict checkpatch; BootROM RST parses cleanly |

The dedicated kernel review regression fixture is
[`review_lifetime_test.c`](../tests/kernel/retained_lifetime/review_lifetime_test.c).
Build the retained-lifetime test module directory against a candidate with
`CONFIG_RETAINED_REVIEW_TEST=m`. This selects a fixture, not a production
kernel configuration option. It requires the v2 NVMEM `priv_owner` API and
`CONFIG_MTD_PARTITIONED_MASTER=n`; the older fixtures remain usable with v1.
The shared initramfs script runs it when the module is included.

The callback-reference test checks the actual reference count inside the
callback and after return. It does not force a concurrent module-unload
race. The partition regression is a deterministic state-transition test,
not an exhaustive SMP publication/removal exploration. BBT recovery and
R852 fixes were checked through their call paths and builds, not injected
hardware failures. Full system suspend was not exercised in QEMU; the host
fixture directly calls the disconnected suspend path.

Review used Sashiko's general, locking, allocation, power-management,
cleanup, build and false-positive guidance. Their checks supplement the
source, model and runtime evidence; they do not guarantee approval.

No board boot, erase or write was performed for this update. The earlier
[physical H616 results](H616-RETAINED-LIFETIME-HARDWARE.md) apply to their
recorded lifetime-v1 integration, not automatically to these new generic
changes. The BootROM views, controller waveform behavior and all other
drivers are not proven by the mtdchar state machine.
