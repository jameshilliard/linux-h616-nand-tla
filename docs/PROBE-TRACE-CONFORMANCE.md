# Source-executed probe traces checked by TLA+

This extension connects the lifecycle C fixture to a separate protocol model.
Previously the C checks and the admission/publication models passed separately.
`NandProbeTrace` now consumes boundary events actually produced while running
the extracted driver and NAND-core bodies. It computes ownership independently
and compares it with snapshots from the C fixture after each observed call.

This is **bounded trace conformance**, not a proof translating arbitrary C
executions to TLA+. In particular, it does not add hardware assumptions or
resolve the separate retained-MTD/unbind lifetime problem.

## Observed boundaries

| Event | Observation point |
| --- | --- |
| `scan-start`, `scan-ok`, `scan-error` | Before/after the fixture's `nand_scan()` wrapper calls actual `nand_scan_with_ids()` and its scan/attach/cleanup functions |
| `link`, `unlink` | After the actual driver calls the host list operation; snapshots traverse the resulting chip list |
| `register-ok`, `register-error`, `unregister` | After the MTD service fixture changes its publication state or returns an injected error |
| `probe-ok`, `probe-error` | Immediately after actual `sunxi_nfc_probe()` returns, before ordinary removal or devres release |
| `finish` | After successful-probe removal or failed-probe unwind, before releasing remaining managed allocations |

Kernel function bodies are neither rewritten nor instrumented. Observers are
in the surrounding service fixtures. Chip identities are node ordinals, not
host pointers or physical board identifiers. Each event records the actual
list membership and fixture MTD publication state. Those snapshots are inputs
to the checker, not values synthesized from its transition function.

The model tracks successful scans, active scanning, list membership, historical
registration and removal. It checks:

- Only available children reach these boundaries.
- Every admitted child's scan finishes before any registration attempt.
- Registration follows scanning and list insertion, without duplicate actions.
- An MTD is unregistered before its chip is removed from the driver list.
- Predicted membership matches both C snapshots after every event.
- Success publishes every available child; errors and final cleanup leave
  no listed or published successfully scanned chip behind.

Internal scan-failure allocations and actual `nand_cleanup()` internals remain
covered by the C harness's allocation/cleanup checks. An `unlink` event alone
is not proof that every hardware or heap resource has been released.

## Input coverage and reproducibility

The trace matrix contains 57 input profiles: zero to three children, every
availability mask, and normal, absent-final-chip, duplicate-CS and empty-reg
variants where children exist. Every profile runs without injection and then
with a single fault at each event observed on that profile's uninjected path.
The consumer independently requires all profiles and the complete recorded
fault-index range, rejects duplicates, verifies the trace digest, and checks
that every observation kind occurs in the fixed candidate's traces.

ASan/UBSan and a separate unsanitized GCOV execution must produce identical
trace bytes. Reports retain the full driver/core digests, extracted-function
digests, generated fixture digest, trace digest and observation counts.

For the integrated v10 candidate, 1,788 C executions project to 45 distinct
boundary traces. Different internal failure sites can produce the same
observable unwind. Deduplication retains a count for every projection and
accounts for every input; it does not drop an unmatched execution. TLC checks
all projections, in wrappers containing at most 32 traces to bound parser
memory. Each model initial state selects one recorded trace and checks every
subsequent event and snapshot. This enumerates the supplied traces, not all
possible kernel schedules.

The unchanged v9 source produces 2,755 trace executions. Its successful
two-child input supplies the required `AllScannedBeforePublish` counterexample.
The other baseline traces are retained but are not claimed to conform to the
new candidate protocol. The original lifecycle suite still reproduces its
expected disabled-child and scan/I/O witnesses.

Seven altered traces must fail their named invariants: disabled-child access,
incorrect snapshots, omitted unregister, omitted unlink, duplicate insertion,
early publication and incomplete successful probe. An unrelated error, parser
failure, timeout or JVM failure is not a passing negative control.
Five handwritten protocol examples and these seven checker mutations are also
part of the default TLC suite. Those unit inputs are explicitly distinct from
the source-generated trace runs.

## Running it

Check the model's unit inputs:

```sh
python3 run.py --case probe-refinement
```

Capture the pinned baseline, then use the result directory printed by the
first command:

```sh
python3 run_lifecycle.py --probe-traces
python3 run_probe_refinement.py results/lifecycle-BASE --expect-baseline
```

With the fixed candidate checked out in a separate kernel repository:

```sh
python3 run_lifecycle.py \
  --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c \
  --expect-available-children --expect-deferred-publication --probe-traces
python3 run_probe_refinement.py results/lifecycle-CANDIDATE
```

## Boundaries still open

The traces observe synchronous fixture boundaries. They do not observe inside
real MTD registration callbacks, schedule kernel threads, model OF reference
ownership or dynamic overlays, or retain MTD users during the cleanup matrix.
Availability is checked at the observed scan boundary, not at every internal
chip-select assignment. Optional ready/busy-property faults can be handled
successfully by the source; not every injected event requires probe failure.

The new matrix injects at most one fault per execution. It does not enumerate
compound failures reached only after earlier recovery changes the call path.
The separate publication model still supplies bounded scan/I/O interleavings;
the separate retained-user model still exposes the unresolved lifetime issue.
Whole-request byte/MMIO traces, physical DMA retirement, randomizer reload,
real SMP/RT schedules and silicon behavior remain separate validation work.
No new kernel change or board access was needed for this extension.
