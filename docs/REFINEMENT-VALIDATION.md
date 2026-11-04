# Command, lifetime, retry and ownership refinements

This extension covers the six follow-up areas selected after the
[current-driver audit](CURRENT-DRIVER.md). It preserves the posted v9 input
and separately tests the v10 sunxi driver identified there. The generic
NAND-core fixes remain separate; they are not silently incorporated into the
core used by these tests. No production kernel changes or hardware operations
are part of this extension.

## Coverage map

| Area | Executable checks | Boundary that remains |
| --- | --- | --- |
| Commands behind composed requests | Real core read/program/column/RAW helpers, internal dispatch, operation parser, sunxi `exec_op()` and IRQ handling, connected to persistent media | Host value adapters join translation units; NAND register effects, ECC results and DMA movement are emulated |
| Timeout recovery | `NandRecoveryRegisters`; actual abort/cleanup/selection with reset retaining or clearing context, same/different chip, failed stop and a late event | Reset and termination register writes do not prove bus retirement or late-event suppression on silicon |
| MTD users and detach | Actual lookup, references, partition removal, device release, read dispatch and sunxi cleanup; `NandDetachPolicy` explores two candidate policies | No actual sysfs/devres or concurrent kernel execution; candidate policies are requirements, not implemented fixes |
| Manufacturer read retry | Actual Hynix/Micron callbacks through commands, both independently and underneath `nand_do_read_ops()`; `NandRetryProtocol` | Parameter acceptance/commit rules are explicit emulator assumptions, not measured chip behavior |
| Byte ownership | Actual core bounce selection and copy bounds; `NandDmaBytes` covers RX/TX, neighboring CPU bytes, cache-line ownership and mapping reuse | Host sanitizers do not emulate the architecture DMA/cache-maintenance implementation |
| Historical negative controls | Seven adapted mutations must fail specific assertions after the unmodified baseline passes | These are modern adaptations, not execution of seven complete historical kernels |

## Connected command execution

`run_transport.py` retains the composed core/BBT/page/ECC/randomizer checks
and replaces their command-level fixtures with value adapters:

```text
core request -> sunxi page/OOB operation -> core NAND command helper
             -> NAND dispatch/parser -> sunxi exec_op/IRQ -> MMIO/media fixture
```

RAW data requests execute `nand_read_page_raw()` or `nand_write_page_raw()`;
RAW OOB requests use the corresponding core command helpers. Both use the
same persistent flash image as corrected reads. The existing 30 profiles
include all PIO/DMA writer-reader pairs, plain/scrambled/vendor formats,
cross-target requests, BBT create/update/reload, transport failures and reuse.

The connected host geometry is two targets, 2-KiB data pages and 256-byte
spares. Additional command tests use both legacy/H616 SRAM/register layouts,
two selects, and hardware-ready versus status-poll paths. A full data-plus-OOB
transfer crosses the real parser's 1-KiB data limit. Each command position is
failed and followed by a new successful read with exact payload comparison:
36 fault/recovery cases, plus successful write/read/column/check-only cases.
The independent low-level suite retains its broader address/SRAM geometry
matrix and the v9 split-address witness/v10 fix check.

All extracted function bodies remain unchanged and are hashed. Translation
units have simplified private structures; adapters pass values and buffers,
never pointers to incompatible kernel structures. Timing macros select an
SDR fixture, not elapsed nanoseconds. Continuous-read and legacy callback
paths are excluded. The connected test does not replace the separate BCH
oracle, DMA provider tests or timing calculations.

## Reset and stop obligations

The recovery model distinguishes PIO, sun4i, sun6i, register MDMA and descriptor
MDMA. External backends terminate through DMAengine; internal backends reset
the controller. Register retention, outstanding bus writes and future command
events are independent inputs. Positive cases include context reprogramming
after either retaining or clearing registers and selection of the same or
another chip. The abstract selection/transfer boundary includes reestablishing
ECC context; it does not claim `sunxi_nfc_select_chip()` alone enables ECC.

The actual-C extension exercises all three capability classes, both reset
retention alternatives and both chip selects. Twelve successful cases restore
spare/timing/select context without an unnecessary clock change. Twelve
failed-stop cases reproduce cleanup unmapping despite an active transfer.
The late-event case injects an old completion after timeout acknowledgement
and demonstrates that a later wait cannot distinguish its generation.

These are conditional hardware-contract witnesses. In particular, the pinned
sun4i/sun6i termination entrypoints return zero; injecting a negative external
termination result does not establish its reachability with those providers.
PIO quiescence and event retirement are assumptions in the positive abstraction,
not a claim that the source PIO timeout path resets the controller. Neither
source execution nor the model proves that a real reset/terminate retires all
bus traffic. The negative cases expose what follows if it does not.

The model's `quarantine` alternative retains the mapping after a failed stop.
It deliberately does not satisfy normal completion or free resources in that
state. It is an ownership-policy sketch, not a production recovery solution.

## Retained MTD references and possible lifetime policies

The C runner now executes 13 unchanged source functions, including actual
`get_mtd_device()`, `mtd_read_oob_std()` and `mtd_release()`. Device-core helpers
remain fixtures, but IDR removal now goes through the actual release body.
Released host object storage is retained for inspection; the test reports an
unsafe resource use instead of deliberately executing a host use-after-free.

The 36 partition schedules include one to three partition levels, zero to two
users, both `CONFIG_MTD_PARTITIONED_MASTER` choices and close-before/after
cleanup. Twelve preserve references through sunxi cleanup. Each allows a later
read dispatch to the already-cleaned resource and a further lookup/open while
publication is retained. Four two-chip schedules check cleanup with each
combination of held users. There are 16 later I/O witnesses in total. Closing
the final reference runs actual release/IDR removal and rejects a new lookup.

`NandDetachPolicy` adds two users on either the same or different chips,
partitions and failure after the first chip was published. Its references are
abstract ownership obligations, not literal counts of every Linux kobject.
The source policy retains publication while users remain and does not defer
resource cleanup. Separate counterexamples cover later I/O and a later open.

Two candidate policies pass the bounded safety/progress checks:

- **Gate and drain:** unpublish/close the entry gate, reject later I/O, wait
  for admitted active operations, then release resources.
- **Defer to last reference:** prevent new opens and retain resources until
  all existing handles close, allowing their operations meanwhile.

Both require an explicit entry/lifetime gate absent from the current cleanup.
The split lookup/acquisition actions pressure-test that candidate gate; the C
test retains the actual MTD table mutex around the source lookup. A policy
cannot be implemented by changing only a local reference counter: all entry
points, partitions, multi-chip probe unwind, devres and shared controller
resources must be handled. No such implementation is claimed here.

## Actual manufacturer retry sequences

`reference/boundaries.json` now pins `nand_hynix.c`, `nand_micron.c` and
`internals.h` to the same kernel revision as the existing core inputs. The
extracted Hynix callback enters parameter mode, writes four fixture-provided
parameter registers, then applies them. Micron uses SET FEATURES at the
read-retry feature address. Runtime retrieval of Hynix's vendor parameter
table and analog correction efficacy are outside this extension.

Failures are injected at every command during initial configuration and
restoring mode zero, both before acceptance and after acceptance with lost
completion. The emulator makes staged/active parameter state visible. Actual
core retry execution covers 42 configurations, including successful retry and
restoration, failed setup, and a good read followed by failed restoration.
The returned error, successful-data prefix length and callback count are
checked; the independent generic retry/OOB bug is not hidden by these tests.

Under the explicit acceptance/retention contract, 20 cases retain staged
parameters and 14 retain active parameters on error. The standalone callback
matrix adds 14 setup failures and 18 failed-restoration cases. An explicit
subsequent successful restore is checked as test cleanup, not inserted into
the source algorithm.

The source-backed conclusion is that returning a transport error does not
execute an implicit rollback. The exact retained chip state depends on
hardware acceptance and commit semantics. These results do not establish a
new successful-read corruption bug, prove the connected board supports these
retry protocols, or justify adding an unconditional reset/replay policy.

## Byte/cache-line ownership

The connected C matrix executes actual core bounce decisions with 4- and
64-byte alignment requirements. It varies all 64 destination offsets, three
page columns and seven lengths around byte, cache-line and page boundaries.
All 2,688 reads compare the complete requested payload and every neighboring
guard byte. The 128 full-page writes verify bounce/direct selection, unchanged
caller storage and complete readback. RAW reads and corrected reads share the
transport tests; these checks exercise the software copies, not physical DMA.

`NandDmaBytes` uses eight distinct byte locations and two four-byte cache
lines. It checks RX delivery, TX publication, isolated bounce storage, exact
copyback, a CPU update to a neighboring byte and an old transfer crossing a
mapping-generation change. The direct unaligned RX case requires exclusive
ownership of the affected cache line. Removing that requirement produces a
neighbor-loss witness; this is a DMA API contract test, not evidence that a
supported sunxi allocation currently violates it. Dropping a copyback,
publishing stale TX bytes or copying an entire line must fail distinct checks.

## Historical mutation controls

The original four reseed/OOB-column/subpage-callback controls remain. Three
additional source regressions are checked:

| Original fix | Adapted regression | Required failure |
| --- | --- | --- |
| `f4c6cd1a7f2275d5bc0e494b21fff26f8dde80f0` | Retain requested strength instead of the selected supported strength | Actual attach strength differs from independent upward selection |
| `f59dab8d9e8f37f7042ba05bfbab748b1312e73e` | Omit the 512-byte ECC-block control bit | Small-page initialization lacks its required bit |
| `28f3d01eca5269b01e33c1bf976d90c33b546ad9` | Treat descriptor preparation failure as ERR_PTR instead of NULL | NULL reaches the submit boundary rather than returning after balanced unmap |

Only generated temporary source is mutated. Each test must abort at its named
assertion, without an unrelated sanitizer error. The unmodified connected
baseline must first pass. Both pinned-source and candidate-source runs are
supported.

## Review and remaining limits

Review checked cached Sashiko general/core, technical-pattern, false-positive,
callstack, locking, I/O-accessor, allocation and alignment guidance. Refreshing
the public prompt content failed; this does not claim its latest revision was
obtained. There was no dedicated NAND prompt in the inspected snapshot.
Actual callers, failure returns and provider capabilities were checked before
classifying results as source findings or conditional hardware witnesses.

All proofs are bounded and depend on explicit contracts; there is no
machine-checked refinement proof from the C to TLA+. Remaining validation
includes actual sysfs/devres/SMP execution, physical DMA/cache ordering,
bus retirement, reset/late-event behavior, manufacturer chip-state semantics
and silicon-generated BCH/randomizer images. Candidate detach/quarantine
policies are not implemented. The known MTD-unbind issue remains unresolved.
Recorded test results and exact limits are in [VALIDATION.md](../VALIDATION.md).

The subsequent [probe trace extension](PROBE-TRACE-CONFORMANCE.md) adds a
direct connection for lifecycle boundaries: actual C observations are fed
to `NandProbeTrace`, which independently computes membership and compares
post-call snapshots. This is bounded trace conformance, not whole-request
or arbitrary-program refinement.
