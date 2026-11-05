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

## Independent RFC implementation

`submit/rawnand-retained-handle-lifetime-v1` is an independent, b4-managed
RFC draft based on NAND `next` at
`30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85`. The draft now contains three
implementation patches, independently of the sunxi OOB submission and the
generic data-range patch:

1. Optional MTD backing-object references and final release, including
   nested partition/device ownership and unavailable-node lookup rejection.
2. A raw-NAND disconnect phase serialized with the chip/controller locks.
3. Sunxi chip/controller ownership that retains metadata and locks until
   the final references disappear.

The intended lifetime contract separates three events:

1. Serialize hardware disconnection with operations already admitted,
   including close-time sync and suspend/resume. Wake suspended waiters;
   this must precede notifiers that may drain those requests.
2. Unregister the MTD devices and drain notifier clients before releasing
   controller resources. Failed post-disconnect operations return `-ENODEV`;
   close-time sync becomes a no-op.
3. Retain the chip, parent/controller lock storage and metadata reachable
   through existing handles until their final references are released.

Waiting for every open file in the remove callback is not a substitute for
this contract: the process requesting unbind can itself hold such a file.
Pinning a module or a parent `struct device` also does not prevent driver
unbind or defer devres cleanup. Merely setting a lockless removed flag does
not drain an operation that already passed its admission check.

The implementation has separate driver, registered-device and acquired-handle
references. A nested partition additionally retains its parent device through
final release: retaining only the master does not protect an intermediate
partition reached by a retained NVMEM/device reference. Sunxi uses heap
allocations for the retained chip, OOB-layout data and controller lock storage;
its hardware resources retain their ordinary teardown order.

Related [SPI-NOR v3 work](https://lore.kernel.org/r/20261022035352.52129-1-tze.yee.ng@altera.com)
uses provider-specific refcounting and removal gating. Its generic
module-reference ordering fix is an external prerequisite of the RFC, with
the exact publicly posted patch ID
`a51b9cb4077105499133c413d58c7ad21d973b38`. It is not republished as a new
James-authored patch. The implementation still needs upstream coordination
over the MTD lifetime API.

### Candidate source checks

`run_retained_lifetime.py` executes the candidate's actual MTD get/put,
lookup, unregister, device-release and master-release functions, character
open/close, raw-NAND disconnect/locking/sync/PM, and sunxi final-release
functions. Unlike the original witness fixture, it frees heap objects so
ASan can detect premature metadata access.

The pinned function bodies in `reference/retained-lifetime.json` come from
candidate commit `198a09362d4e14f357c79bced795325e36b08fab`. The manifest records
the source paths, full-file hashes and per-function hashes without local
workspace paths. Use `--kernel-tree` to check another applied candidate.

The current 101 schedules exercise 202 file opens/closes and cover:

- Registered masters, partition-only registration and nested partitions.
- Read-only/writable files and two open handles.
- Close before/after unbind and rejected late opens.
- Failed file allocation and failed module acquisition.
- Immediate/delayed device release and extra device references.
- Ordinary unbind and module-going release with no open handles.
- Suspended-reader wakeup, rejected suspend and harmless resume after removal.
- Two chips sharing a controller, released in different close operations.

Registration itself, devres, module accounting and device-core primitives are
explicit shims. The registration-failure completion callback is exercised,
but the full registration/probe failure paths are source-audited rather than
executed by this runner. This is not a real kernel unbind or fault-injection
test.

### Candidate interleavings

`NandRetainedLifetime` separates disconnection, unpublication, initial-reference
drop, hardware teardown, device release and metadata release. Two users can
interleave read admission, controller locking, writable close and reference
drops. Both initially running and initially suspended chips are checked, with
one or three registered devices.

Eight TLC configurations match expectations: four candidate cases satisfy
storage lifetime, parent lifetime, hardware draining, exclusivity and eventual
release; four negative controls detect missing draining, missing device
retention, missing nested-parent retention, or missing suspend wakeup.
The older witness runner still reproduces the original unsafe close paths.

```sh
python3 run_retained_lifetime.py
python3 run_retained_model.py --download
python3 run_retained_lifetime.py --kernel-tree ../linux
```

### Review and remaining validation

The candidate objects build with `W=1` on x86-64 COMPILE_TEST, with
`MTD_PARTITIONED_MASTER` enabled and disabled. Checkpatch reports no warnings
or errors for the three commits. b4 resolves the published prerequisite and
applies it and the three outgoing patches to the declared NAND-next base.
Dry-run export produces four messages: a cover and three RFC patches.

The review used Sashiko's technical-patterns, false-positive, callstack and
pointer-guard guidance, plus locking, allocation, sysfs, VFS, PM and build
prompts. There is no dedicated MTD prompt in that bundle. In particular:

- All `nand_get_device()` callers propagate failure, including close-time sync.
- The disconnect path takes chip then controller locks and does not wait for
  open files. It precedes unregister so suspended notifier requests can drain.
- Panic-write notifier clients are unregistered before hardware destruction;
  a lockless disconnected check alone is not a general drain mechanism.
- The master callback caches module/completion state before freeing its object;
  nested parent device references survive until child release.
- Attach failures free the converted heap allocation even when NAND scan does
  not call detach. Successful NAND cleanup only destroys retained metadata.

Remaining work before calling the implementation fully validated:

- Real kernel lockdep/KASAN unbind/rebind, writable-close and NVMEM tests.
- Full allocation/registration-failure injection, including partial publication
  and delayed kobject release during probe failure.
- Virtual-concat lifetime/error paths: these have independent ownership issues
  in the base code and are not modeled or converted by this RFC. The generic
  infrastructure is opt-in; this is not a claim that every MTD backend now
  survives unbind.

The bounded model and host shims do not prove complete kernel correctness or
Sashiko approval. No hardware was unbound or modified during these checks.
