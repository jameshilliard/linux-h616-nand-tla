# sunxi NAND driver validation

Executable TLA+ models and source-extracted C checks for the Linux Allwinner
NAND OOB series. The models concentrate on the interactions that are difficult
to review locally: logical ECC steps versus controller slots, packed OOB
layouts, DMA batching, error/pattern classification, interrupt completion reuse,
and failure cleanup. The extended suite also covers the DMA backends, command
parser, SRAM/media identity, NAND-core contracts, shared-controller lifecycle,
probe unwinding, and timing calculations. Integration checks cover BBM/BBT
lifecycle, real MTD request/retry code, stored-byte format equivalence, RT IRQ
draining, DMA visibility, device program budgets and interrupted BBT updates.
Deeper checks execute scan/cleanup and ONFI negotiation, compare pre-series and
BSP layouts, compose real BCH correction with the driver, and inject compound
read/BBT failures.
Boundary checks connect real core requests to persistent sunxi page operations,
decode ONFI/ID values into attach decisions, execute MTD reference/partition
removal and provider IRQ/tasklet paths, and detect historical bug patterns.
The [refinement checks](docs/REFINEMENT-VALIDATION.md) connect requests to real
command helpers and `exec_op()`, execute Hynix/Micron retry callbacks, and
separate reset/stop, MTD lifetime and byte/cache-line ownership contracts.
The [hardware-contract audit](docs/HARDWARE-CONTRACTS.md) ties register behavior
to manuals, pinned vendor code and narrowly scoped prior board observations.
It corrects the earlier SRAM/count conflation and records a large-OOB
source/manual mismatch rather than treating that old fixture result as proof.
Generation-specific A10/A23/H616 profiles distinguish advertised SRAM from
RAM0 bounds, valid older requests from H616's ten-bit counter, and external,
register and descriptor DMA. The updated candidate includes the count-limit
fix; the pinned submission retains its reproducible counterexamples.
The [stream, aperture and persistent-context checks](docs/STREAM-APERTURE-CONTEXT.md)
add independent raw-byte randomizer oracles, DMA address-range constraints and
packed user-data register reuse across PIO/DMA requests. They also expose how
a matching randomizer error can pass a write/read round trip.
The [probe-visibility checks](docs/PROBE-VISIBILITY.md) exercise DT child
availability and scanning a second chip while the first MTD is already usable.
They reproduce two independent probe-path defects in the original snapshots.
Filtering and deferred-publication fixes are checked separately and together
against the pre-series NAND base, and are now carried as patches 20 and 21
of the 23-patch sunxi v10 draft. The source fixtures also test partial
registration cleanup and require deliberate regressions to fail. Fixes outside
the sunxi NAND driver retain separate submission branches.
The [probe trace checker](docs/PROBE-TRACE-CONFORMANCE.md) now feeds actual
source-extracted scan/list/publication/unwind observations into TLA+, comparing
model ownership with the C fixture's snapshots after each event.
The [recovery trace checker](docs/RECOVERY-TRACE-CONFORMANCE.md) connects actual
DMA/IRQ/abort/cleanup helper execution to independent ownership and register
checks, including timeout races and explicit late-event/retirement assumptions.
The [randomized OOB pipeline checker](docs/OOB-PIPELINE-CONFORMANCE.md) connects
decoded metadata, batch ECC status, erased-page decisions, raw parity/tail
reads and final bytes/accounting to an independent TLA+ stage model.
The [whole-request traces](docs/REQUEST-TRACE-CONFORMANCE.md) connect actual
page/OOB callers to DMA, interrupt recovery and persistent-media observations,
including compound read failures and subsequent controller reuse. The
[file-lifetime checks](docs/FD-LIFETIME-CONFORMANCE.md) follow actual MTD
open/close into NAND locks and expose close/sync within the known unbind issue.

The [2026-11-04 real H616 measurements](docs/HARDWARE-MEASUREMENTS-20261104.md)
add a restricted diagnostic module, sanitized captures and offline replay.
They confirm count/SRAM/BCH contracts but disprove the earlier continuous
normal-command randomizer assumption. The measured restart, direction and
non-word-tail behavior now has its own TLA+ and actual-source checks. Older
controller behavior and genuinely failed DMA/reset retirement remain unproved.

The reference is the [posted v9 series, 18 patches](https://lore.kernel.org/r/20261017-submit-sunxi-nand-vendor-oob-layout-v1-v9-0-b624c09e3ee5@gmail.com).
The v10 draft inspected when this repository was created has the same driver
implementation. This repository validates that pinned implementation, **not
whatever happens to be in the latest kernel tree**.

Optional [candidate-fix checks](docs/CANDIDATE-FIXES.md) exercise local fixes
alongside that baseline, with separate source hashes and positive regression
expectations. They do not replace the pinned submission or its counterexamples.
The [current-driver audit](docs/CURRENT-DRIVER.md) maps the updated sunxi
draft to the source tests and the chip-selection model, including remaining
branch gaps and hardware/lifetime limitations.

## Run

Requirements: Python 3.10+, Java 17+ (the CI template uses 21), and GCC with its
matching `gcov`, AddressSanitizer and UndefinedBehaviorSanitizer. The ordinary
host runners need no board, root privileges or kernel build and do not access
NAND. The optional tools under [hardware/](hardware/README.md) are separate,
explicitly authorized board tests, including a destructive backup-block phase.

```sh
python3 run.py --download
python3 run_c.py
python3 run_lowlevel.py
python3 run_core.py
python3 run_lifecycle.py
python3 run_providers.py
python3 run_codec.py
python3 run_oob_pipeline.py --download
python3 run_composed.py
python3 run_transport.py
python3 run_transport.py --count-boundary
python3 run_discovery_boundary.py
python3 run_lifetime_boundary.py
python3 run_provider_boundary.py
python3 run_historical.py
python3 run_mtd_boundary.py
python3 run_ordering.py
python3 run_request_refinement.py
python3 run_fd_locking.py
python3 run_fd_lifetime.py
python3 run_hardware.py
```

`--download` fetches the official TLA+ tools v1.8.0 prerelease asset and verifies
its pinned SHA-256. The exact build and asset ID are recorded in `source.json`;
an upstream prerelease replacement will fail verification, not silently change
the model checker. To use an existing matching copy:

```sh
python3 run.py --jar /path/to/tla2tools.jar
python3 run.py --list
python3 run.py --case irq --jar /path/to/tla2tools.jar
```

Results, generated TLC configurations, counterexample traces, extracted C,
compiler diagnostics, and machine-readable summaries go in ignored `results/`
subdirectories. A timeout, syntax error, unexpected invariant violation, or
sanitizer failure fails the run. Expected counterexamples must violate their
specific named invariant (or the named termination property for the IRQ
starvation case); an arbitrary TLC failure does not count as success.
The provider runner has one documented, function-local UBSan exclusion for an
unused null-derived address in the pinned `vchan_complete()`; see the
[boundary validation map](docs/BOUNDARY-VALIDATION.md).

## What is checked

| Model | Main properties |
| --- | --- |
| `NandBatch` | Disjoint OOB regions; 128-byte variable-user-data bank limit; logical-step lengths; packed length registers; page-relative DMA columns; exactly-once step processing |
| `NandOob` | Unprotected-read coalescing preserves protected bytes; parity and tail use their intended representations |
| `NandIrq` | No stale register writes or completion reuse across two operations; timeout drains the handler and rechecks remaining events; polling completion |
| `NandTransfer` | No unmap while DMA remains active; no replay of partially written pages; one program confirm; cleanup and read-stat rollback |
| `NandEcc` | Transfer-local status/counter caches; capture before raw rereads; pattern synthesis; vendor classification precedence; decoded-data preservation; correction/failure accounting |
| `NandController` | Combined DMA/IRQ/timeout recovery, repeated failures, PIO read retry, program-confirm rules and operation reuse |
| `NandDma` | External DMAengine, register MDMA and descriptor MDMA; mapping/unmapping, validation, descriptor publication and block encoding |
| `NandCommand` | Split data/address offsets, driver SRAM limits versus hardware RAM0/count contracts, generation-specific requests, write staging, readiness, check-only and errors |
| `NandRegisterProtocol` | FIFO acceptance versus FSM/DMA completion, W1C and automatic event clearing, readiness and interrupt masking |
| `NandMedia` | Distinct page/column payloads, NAND bit programming, partial failures, protected-byte preservation and readback |
| `NandCore` | Format admission, whole-page vendor classification, core error interpretation, read retries and counter wrapping |
| `NandLifecycle` | Two chips sharing a controller; chip context, users/IRQ draining and resource lifetime |
| `NandProbe` | Probe/scan/register failures, managed/manual ownership, DMA fallback/defer and chip-select allocation |
| `NandTiming` | Timing lookup, ceiling arithmetic, minimal encodings, check-only and clock-rounding contracts |
| `NandRequest` | Core data/OOB copy ordering, read retry, ECC statistics, return lengths and transport-error prefixes |
| `NandBbt` | Factory-marker scan, RAM/flash tables, runtime markbad, optional OOB markers, mirrored updates and remount |
| `NandFormat` | Page-local PIO/DMA batching, seed selection, stored segment identity and reader compatibility |
| `NandOwnership` | CPU/device visibility, descriptor publication, streaming-map ownership and backend register restrictions |
| `NandPersistence` | Interrupted mirrored BBT erase/program, eight-bit version rollover and detectable/undetected torn tables |
| `NandRt` | Fine-grained threaded-IRQ draining and safety versus eventual scheduling/liveness |
| `NandProgram` | Finite partial-program budgets, bit monotonicity, erase, torn programs and no implicit write replay |
| `NandRecovery` | Backend stop, outstanding bus transactions, IRQ drain and late controller completion are separate obligations |
| `NandNegotiation` | Bootloader timing handoff, SET/GET_FEATURES, SDR mode changes, fallback and failed recovery |
| `NandRelocation` | BBT replacement failure/exhaustion, mirror separation, restart and torn-table rejection |
| `NandSequence` | Persistent chip/seed/column/spare state and enable-bit cleanup across mixed operations |
| `NandSelection` | Equal/different-rate chip switches, repeated clock failures, check-only purity and error propagation in both operation callers |
| `NandMtdLifetime` | Registration references versus open users and NAND resource lifetime |
| `NandProvider` | Command completion, provider IRQ/tasklet completion, memory drain and termination; explicit no-client-callback case |
| `NandDiscovery` | Parameter-copy admission, geometry requirements and callback publication |
| `NandProviderBanks` | Physical-channel ownership and complete IRQ-bank dispatch |
| `NandCyclicLifetime` | IRQ/tasklet/termination lifetime for one DMA virtual channel; adjacent provider work, not a NAND transfer mode |
| `NandRecoveryRegisters` | Backend-specific stop/reset, retained or cleared registers, same/different-chip reuse, outstanding writes and late events |
| `NandDetachPolicy` | Two users on the same/different chips, retained publication, partition references, partial probe and alternative lifetime policies |
| `NandDmaBytes` | RX/TX payload publication, exact bounce copyback, neighboring cache-line bytes and mapping-generation reuse |
| `NandRetryProtocol` | Hynix enter/write/apply and Micron SET FEATURES, accepted-but-failed commands, retry reads and failed default restoration |
| `NandRandomizerStream` | Independent stored/read representations, phase across raw command boundaries, seed changes, zero-length requests and partial errors |
| `NandDmaAperture` | 32-bit buffer/descriptor endpoints, mask/alignment/caller contracts, exact address encoding and balanced mapping cleanup |
| `NandUserDataContext` | Persistent packed lengths across PIO/DMA requests, logical-step offsets, zero/unused slots, word boundaries and absent legacy registers |
| `NandChildAdmission` | Available DT children, chip-select claims, absent devices, registration errors and probe rollback |
| `NandProbePublication` | Scan-versus-published-I/O chip/context interference; scan-before-publication ordering, supplemented by candidate C checks |
| `NandProbeTrace` | Source-generated probe/unwind traces, independent membership transitions, actual fixture snapshots and checker mutation controls |
| `NandRecoveryTrace` | Source-helper DMA/wait/abort/reuse traces, interrupt cleanup, mapping ownership and explicit retirement/event-causality assumptions; dedicated runner |
| `NandOobPipeline` | Source-helper decoded-metadata/ECC/classification/raw-read observations, byte-exact PIO/DMA representations and error/statistics ordering; dedicated runner |
| `NandMtdBoundary` | Public request normalization/admission, partition offsets, return lengths, ECC statistics and explicit EOF/arithmetic witnesses |
| `NandRequestTrace` | Actual page/OOB/subpage caller observations, DMA ownership, compound errors, single confirm and request results; dedicated runner |
| `NandRequestOrdering` | Two operations with fine-grained IRQ/timeout interleavings, physical retirement and old-event assumptions; dedicated runner |
| `NandFdTrace` | Actual MTD open/close references, retained publication and close/sync after cleanup; dedicated runner |
| `NandFdLocking` | Chip/controller lock order, two users, unbind and hypothetical lifetime policies including close/sync; dedicated runner |

The driver harnesses now extract all 90 function bodies **unchanged** from the
bundled submitted driver, verify its hash, then compile them with host fixtures.
The low-level harness also executes the actual NAND-core operation parser and
suboperation helpers from the same revision. These harnesses supplement the
models; they are not full NAND controller emulators or kernel integration tests.
The core C harness executes 46 unchanged NAND-core/MTD/BBT functions against a
finite, fault-injectable NAND fixture. It checks actual core control flow, not
only an abstract contract. The stored-byte tests exercise source randomizer,
metadata and batching helpers across 168 PIO/DMA writer-reader combinations;
their synthetic parity is not a BCH encoder. `run_codec.py` adds independent
polynomial encoding, unmodified Linux BCH decoding and sunxi-tools page
encoding, then runs additional real-codec vectors through the driver harness.
`run_lifecycle.py` adds actual scan/cleanup and timing-mode negotiation;
`run_providers.py` executes external DMA config/stop entrypoints. These retain
explicit kernel-service and hardware boundaries.
`run_composed.py` connects core and driver bodies through value adapters and a
persistent media/register fixture. The discovery, lifetime and provider-boundary
runners replace additional earlier fixtures with pinned source bodies.
`run_transport.py` additionally executes the real core command helpers, RAW
page helpers, dispatch, parser, sunxi `exec_op()` and IRQ bodies. Its core
retry loop reaches actual manufacturer callbacks; its DMA data movement and
hardware parameter effects remain explicit fixtures.
`run_historical.py` requires seven adapted historical regressions to fail their
specific checks, after first passing the unmodified baseline.

The standard suite has 875 TLC configurations across 42 models, including positive checks,
deliberate mutations, hardware/core assumption tests and source-contract
counterexamples. The dedicated probe trace runner additionally checks captured
C executions, with input-specific counts. Other dedicated trace and ordering
runners have separate counts. For a fixed v10 candidate, `run_suite.py` provides
one snapshot-based full rerun; see the [request guide](docs/REQUEST-TRACE-CONFORMANCE.md).
See [the recorded validation](VALIDATION.md),
[the seven-area coverage map](docs/EXTENDED-COVERAGE.md),
[the integration coverage map](docs/INTEGRATION-COVERAGE.md),
[the six-area deep-validation map](docs/DEEP-VALIDATION.md),
[the boundary and historical-regression map](docs/BOUNDARY-VALIDATION.md),
[the command/lifetime/ownership refinement map](docs/REFINEMENT-VALIDATION.md),
[patch-by-patch coverage](docs/COVERAGE.md),
[source-level findings](docs/FINDINGS.md), and
[assumptions and limits](docs/ASSUMPTIONS.md).

The source-extraction runners also make a separate, unsanitized GCOV build.
To combine the driver runners' source-mapped coverage reports, substitute the
result directories printed by the runners:

```sh
python3 coverage_report.py results/c-RUN/summary.json results/lowlevel-RUN/summary.json results/lifecycle-RUN/summary.json
```

This reports unexecuted functions and missed source lines explicitly. GCOV
coverage of extracted functions under host fixtures is not whole-kernel branch
coverage; TLC action coverage is a separate measure.
The core runner records its multi-file coverage separately in its own summary,
and fails if any extracted core function was not executed.
The codec runner invokes `run_c.py` with generated binary vectors; it and the
ONFI boundary fixture currently require a little-endian host. It records parity/image/correction results rather
than claiming exhaustive branch coverage of the BCH library.

## Interpreting the result

Passing is bounded evidence about these models and these C fixtures. It is not
a proof of all executions of the Linux driver, hardware correctness, NAND media
reliability, or complete kernel integration. There is no mechanically verified
C-to-TLA+ refinement mapping.

In particular, cleanup relies on reset/termination actually stopping DMA. The
negative assumption tests demonstrate why this matters; they are **not evidence
of an observed hardware failure or a new regression in the submission**.
Some tests reproduce conditional source-level failures. A passing test for an
expected counterexample means the failure was reproduced, not that the driver
was fixed. Reachability and outstanding hardware contracts are documented.

## Source provenance and maintenance

`source.json` records the posted message ID, base, sent tip, driver/parser
digests, and TLC version/digest. `reference/` contains the exact submitted driver
and binding, unchanged NAND-core parser excerpts, and the matching full
NAND-core/MTD/BBT source inputs, with original licensing.
Additional manifests under `reference/codec`, `reference/compat` and
`reference/providers` pin the independent comparison inputs and DMA providers.
`reference/boundaries.json` pins ONFI, MTD partition, virt-dma, manufacturer
retry and internal NAND dispatch inputs.
Source functions and their hashes are listed in each C-run summary. The
reference files are inputs, not a second driver to edit.

When changing the reference revision, replace it from the actual submitted
series, update `source.json`, review the function-to-model mapping, and rerun all
applicable source, trace and model runners. Do not update a digest merely to
make a mismatch disappear.
Changes to hardware assumptions require independent documentation or
measurements, not just a model that produces the desired result.

## Optional CI

[ci/validate.yml](ci/validate.yml) is a pinned GitHub Actions workflow template.
It runs all thirteen suites on Ubuntu 24.04 with Java 21 and retains diagnostics and
counterexample traces. It is intentionally outside `.github/workflows/` in this
initial publication because the publishing credential cannot install workflows.
Automatic CI is not enabled. Install it as `.github/workflows/validate.yml` using
a workflow-authorized credential to activate it.

Repository-authored code is GPL-2.0-or-later. See `LICENSE`; the reference binding
retains its dual-license SPDX notice, and the NAND-core excerpts retain their
GPL-2.0-only notices.
