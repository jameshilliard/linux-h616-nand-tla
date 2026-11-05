# MTD file lifetime, NAND locks and unbind

The earlier retained-MTD witness is now connected to actual character-device
open/close and NAND locking paths. This strengthens the existing lifetime
finding; it does not establish a separate OOB-series regression or implement
a lifetime fix.

## Source execution

`run_fd_lifetime.py` extracts unchanged `mtdchar_open()`, `mtdchar_close()`,
MTD lookup/get/put/unregister/partition release, `mtd_sync()`, `nand_sync()`,
`nand_get_device()`, `nand_release_device()` and sunxi chip cleanup bodies.
The input files remain hash-pinned; only the sunxi driver can be replaced by
an explicitly supplied candidate.

Sixteen schedules cover master/partition handles, read-only/writable files,
one/two users and close-before/after-unregister. They execute 32 successful
opens and closes, eight additional opens while a retained reference keeps the
removed device published, and four close/sync lifetime witnesses.

Closing a writable MTD handle invokes `mtd_sync()` before releasing its
reference. For raw NAND this reaches `nand_sync()`, which locks the chip and
shared controller. If driver cleanup and subsequent managed-resource release
have already occurred, merely closing the handle reaches dead lock storage;
another explicit read/write request is not necessary.

The host fixture retains storage and records these invalid-resource accesses
without invoking actual undefined behavior. It does not execute device-core
sysfs unbind or devres release. Resource flags and dead lock addresses are the
explicit boundary between the unchanged cleanup/open/close functions and the
kernel allocation lifetime. The other lifecycle runner separately executes
the actual NAND allocation/cleanup implementation.

`NandFdTrace` independently accounts for registration/user references,
publication, resource release and writable-close lock accesses, comparing each
transition with source observations. One integrity wrapper covers all sixteen
traces. Four expected lifetime violations and three snapshot-sensitivity
controls bring the dedicated checker to eight configurations. ASan/UBSan and
GCOV must produce the same trace bytes.

## Two-user lock model

`NandFdLocking` follows the actual chip-then-controller acquisition order and
reverse release order for two users on the same or different chips. Read
operations, close/sync, final reference drops and unbind can interleave.

The model checks exclusive controller ownership and lifetime of the locks
themselves, not only permission to access hardware. Its nine configurations
include the current cleanup mismatch and two hypothetical safe policies:
retain resources until the last reference, or gate every relevant entrypoint
and drain admitted operations before releasing resources. Gating reads/writes
but overlooking close/sync still fails. Omitting the shared controller lock
allows two different-chip users to overlap.

These policies are specification alternatives, not implemented fixes. They
do not settle module/device ownership, sysfs removal semantics, nested
partition lifetime, every ioctl, suspension, or full kernel lockdep behavior.
The bounded model cannot substitute for auditing and testing an eventual
generic MTD/driver lifetime solution.

## Reproduce

```sh
python3 run_fd_locking.py --download
python3 run_fd_lifetime.py
python3 run_fd_lifetime.py \
  --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
```

Saved source observations can be checked with `--results results/fd-lifetime-RUN`.
An expected counterexample passes only when its specifically named invariant
fails; parser errors, sanitizer failures and unrelated violations are failures.
No live unbind, flash write or production kernel change is performed.

## Independent fix preparation

`submit/rawnand-retained-handle-lifetime-v1` is an independent, b4-managed
RFC draft based on NAND `next` at
`30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85`. It currently contains tracking
metadata only, not an implemented or validated kernel fix. It does not
depend on the sunxi OOB submission or the generic data-range patch.

The intended lifetime contract separates three events:

1. Unpublish the device and reject new clients, while allowing removal
   notifiers to finish any required flushes.
2. Serialize hardware disconnection with operations already admitted,
   including close-time sync and suspend/resume, before releasing controller
   resources.
3. Retain the chip, parent/controller lock storage and metadata reachable
   through existing handles until their final references are released.

Waiting for every open file in the remove callback is not a substitute for
this contract: the process requesting unbind can itself hold such a file.
Pinning a module or a parent `struct device` also does not prevent driver
unbind or defer devres cleanup. Merely setting a lockless removed flag does
not drain an operation that already passed its admission check.

The implementation must account for successful and partially failed
registration, nested partitions, both values of `MTD_PARTITIONED_MASTER`,
multi-chip controllers, rejected new opens, and metadata/device references
outside ordinary read/write calls. A generic lookup-only change would not
repair the retained-handle witness reproduced here.

Related [SPI-NOR v3 work](https://lore.kernel.org/r/20261022035352.52129-1-tze.yee.ng@altera.com)
uses provider-specific refcounting and removal gating. It includes a generic
module-reference ordering fix, but its driver conversion does not cover raw
NAND or sunxi. Its discussion also leaves device/sysfs lifetime questions
open. This work should be coordinated with that submission rather than
reposting the generic fix as a new independently authored change.

The existing positive lifetime models remain proposed contracts, not proof
that this draft branch fixes the kernel. Candidate source conformance,
concurrent fault-injection tests and removal/rebind testing remain required.
