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

The reference is the [posted v9 series, 18 patches](https://lore.kernel.org/r/20261017-submit-sunxi-nand-vendor-oob-layout-v1-v9-0-b624c09e3ee5@gmail.com).
The v10 draft inspected when this repository was created has the same driver
implementation. This repository validates that pinned implementation, **not
whatever happens to be in the latest kernel tree**.

## Run

Requirements: Python 3.10+, Java 17+ (the CI template uses 21), and GCC with its
matching `gcov`, AddressSanitizer and UndefinedBehaviorSanitizer. No board, root
privileges, or kernel build is needed. The tests do not access NAND devices.

```sh
python3 run.py --download
python3 run_c.py
python3 run_lowlevel.py
python3 run_core.py
python3 run_lifecycle.py
python3 run_providers.py
python3 run_codec.py
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
| `NandCommand` | Split data/address offsets, SRAM bounds, write staging, read readiness, check-only and errors |
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

The suite has 356 TLC configurations across 23 models, including positive checks,
deliberate mutations, hardware/core assumption tests and source-contract
counterexamples. See [the recorded validation](VALIDATION.md),
[the seven-area coverage map](docs/EXTENDED-COVERAGE.md),
[the integration coverage map](docs/INTEGRATION-COVERAGE.md),
[the six-area deep-validation map](docs/DEEP-VALIDATION.md),
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
The codec runner invokes `run_c.py` with generated binary vectors; it currently
requires a little-endian host. It records parity/image/correction results rather
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
Source functions and their hashes are listed in each C-run summary. The
reference files are inputs, not a second driver to edit.

When changing the reference revision, replace it from the actual submitted
series, update `source.json`, review the function-to-model mapping, and rerun all
seven runners. Do not update a digest merely to make a mismatch disappear.
Changes to hardware assumptions require independent documentation or
measurements, not just a model that produces the desired result.

## Optional CI

[ci/validate.yml](ci/validate.yml) is a pinned GitHub Actions workflow template.
It runs all seven suites on Ubuntu 24.04 with Java 21 and retains diagnostics and
counterexample traces. It is intentionally outside `.github/workflows/` in this
initial publication because the publishing credential cannot install workflows.
Automatic CI is not enabled. Install it as `.github/workflows/validate.yml` using
a workflow-authorized credential to activate it.

Repository-authored code is GPL-2.0-or-later. See `LICENSE`; the reference binding
retains its dual-license SPDX notice, and the NAND-core excerpts retain their
GPL-2.0-only notices.
