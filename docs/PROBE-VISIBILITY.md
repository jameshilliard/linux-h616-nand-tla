# NAND child admission and probe publication

This extension closes two source-level gaps in the earlier lifecycle models:
which DT children are admitted, and whether scanning can overlap I/O on an
already-published MTD. It does not assume that the parent driver's probe lock
also protects MTD operations.

## Source versions

Both findings reproduce in the unchanged posted v9 driver and the current v10
draft at `ccdee2ee49862a1377d31f5c541ce9d5fab327df`. The latter's driver SHA-256
is `7e2525a0097a156a7a4e952a073c03ca19872f1c417a42387f15d5b85081a50a`.
The pre-series base, `30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85`, also uses the
same child iteration and per-child scan/register ordering. Neither finding
depends on randomized OOB layout or on the new command-count fix.

The pinned [sunxi source](../reference/sunxi_nand.c),
[NAND core](../reference/nand_base.c) and
[MTD core](../reference/mtdcore.c) remain unmodified. New tests do not stand in
for production fixes. See findings 12 and 13 in [FINDINGS.md](FINDINGS.md).

## Unavailable DT children are scanned

`sunxi_nand_chips_init()` iterates with `for_each_child_of_node_scoped()`.
`sunxi_nand_chip_init()` then claims the child's chip selects, calls
`nand_scan()` and registers its MTD. Neither that function nor the NAND core's
`rawnand_dt_init()` and scan path checks node availability.

The OF availability rule accepts a missing `status` property, `"okay"` and
`"ok"`. Other status values are unavailable, including `"disabled"`,
`"reserved"` and failure statuses. These are valid DT states, not malformed
geometry or an unsupported helper argument.

Two consequences are directly observable in the source fixture:

- An unavailable but populated child is scanned and registered anyway.
- An unavailable, absent second chip fails detection and causes the driver to
  unregister the first, usable child and fail the controller's probe.

The lifecycle fixture executes the actual probe, child initialization,
`nand_scan_with_ids()`, `nand_scan_ident()` and cleanup bodies. Its detector
supplies presence/absence and records whether an unavailable node reached it;
it does not claim to execute physical READ ID. All seven status variants,
both child positions and both presence states are exercised: 28 cases with
16 unavailable-child scan witnesses in each unchanged driver version.

`NandChildAdmission` checks 32 positive filtering-policy cases and two current
source-contract counterexamples. It also covers duplicate chip selects,
registration failure, no children and all-disabled children. The availability
predicate is grounded in `__of_device_is_available()` in `drivers/of/base.c`.
OF reference counts and concurrent DT overlays are outside this model.

An independent driver fix can use the scoped **available-child** iterator.
That must happen before chip-select allocation and scan. The existing scoped
reference handling should be retained. `run_lifecycle.py` has an explicit
`--expect-available-children` switch for checking such a candidate; baseline
expectations deliberately continue to reproduce the existing defect. No
kernel implementation of that fix is included in this validation update.

## A published MTD can race with the next scan

The driver scans and registers each child before moving to the next one.
Registration exposes the first device while the second child's identification
still accesses the same controller without its mutex.

| Boundary | Actual source behavior |
| --- | --- |
| `sunxi_nand_chip_init()` | Calls `nand_scan()` and then `mtd_device_register()` for each child |
| `add_mtd_device()` | Publishes the device/IDR and notifies consumers; releases `mtd_table_mutex` before returning to the probe loop |
| `mtdchar_open()` / `get_mtd_device()` | Obtain the MTD reference without taking the NFC parent device's probe lock |
| `nand_read_oob()` / `nand_get_device()` | Normal I/O takes the chip mutex and shared `controller->lock` |
| `nand_scan_with_ids()` / `nand_scan_ident()` | Do not acquire the controller mutex before detection; initializing the new chip's mutex does not lock the controller |
| `nand_detect()` | Calls `nand_reset()` and READ ID, reaching controller operations without `nand_get_device()` |
| `sunxi_nfc_select_chip()` | Writes shared chip select, page/spare configuration and timing; may change the controller clock |
| `sunxi_nfc_read_buf()` | Launches a data command using the current controller context, without independently reselecting its chip |

A failing schedule needs neither unbind nor probe failure:

1. Child A's MTD is published. Its user acquires the normal NAND locks and
   selects A for I/O.
2. Probe begins child B's identification. Its controller operation selects B,
   replacing A's chip select and timing while A still owns the mutex.
3. A resumes and launches its data command under B's context.

The scanner can likewise have its context overwritten by a normal user.
This requires separate NAND child nodes on one controller; scanning several
targets in a single child occurs before that child's MTD is registered.
A built-in controller driver is sufficient, without assumptions about module
loading state or administrative removal.

### Executed evidence and abstraction boundary

The lifecycle fixture schedules a first-chip user at its publication boundary
and executes the unchanged `nand_get_device()` to acquire the real core's
chip/controller lock sequence. The subsequent real scan body reaches the
detector while that lock is held. Mutex and publication services are explicit
host fixtures, not real kernel threads. The detector's entry is instrumented;
its RESET/READ ID implementation is checked by source inspection.

The low-level fixture then replays the interfering register schedule with
actual `sunxi_nfc_select_chip()`, `sunxi_nfc_exec_op()` and
`sunxi_nfc_read_buf()` bodies. A command for B is followed by A's raw read;
the read's command launch still has B's CS and timing. Twelve schedules cover
A10/A23/H616, equal/different clock rates and a configured ready/busy pin
versus no dedicated pin.
These are wrongly selected command launches, **not measured physical data
corruption or a fully integrated concurrent kernel reproduction**.

`NandProbePublication` explores one to three children and a bounded user
operation. Four current-ordering instances reproduce `NoContextInterference`;
single-child/no-user controls pass. Twenty-nine positive instances scan all
children before publishing any, with scan/registration failure positions
varied. All eight nonterminal action types are required to be reached.

Deferred publication is a **model-only fix direction**. A kernel implementation
must distinguish successfully scanned but unregistered chips from registered
ones during cleanup. Partial registration failures still need careful unwind;
this model deliberately does not free resources to hide or solve the separate
open-MTD/unbind lifetime problem. Locking around all of `nand_scan()` is not
an equivalent quick fix: its later BBT work can use normal MTD operations that
acquire the controller mutex themselves.

## Review and remaining gaps

The audit used Sashiko's cached technical-pattern, false-positive, callstack,
OF, locking, I/O-accessor and cleanup guidance at prompt revision
`d048f8742f6740c38833adf02e87901412d771ca`. There is no dedicated NAND prompt
in that set. This is not a bot run or a claim of Sashiko approval.

In particular, the review checked the two strongest possible objections:
child availability is not filtered downstream, and the parent's probe lock is
not acquired by the MTD user path. The normal I/O mutex does not serialize an
unlocked scan. All objects can remain live throughout the second witness, so
it does not rely on an inferred use-after-free. Targeted public searches found
no matching fix, but do not establish exhaustive mailing-list coverage.

Remaining high-value work is still separate from these two findings:

- Hardware reset/stop versus outstanding DMA bus transactions and late events.
- Hidden randomizer reload and physical stored-image compatibility across
  command boundaries; a same-chunk round trip cannot establish this.
- Physical SRAM extent/aliasing and incompletely documented older NFC details.
- Open-MTD resource lifetime, real SMP/RT schedules and partial publication
  teardown, including consumers that retain references after registration.
- End-to-end C/model trace correspondence and actual NAND-part error/retry and
  power-loss behavior.

The added models have bounded domains and explicitly abstract kernel services.
They expand coverage without asserting full hardware or whole-driver proof.
No board access, kernel edit or NAND-content change was performed.
