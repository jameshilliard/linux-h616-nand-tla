# sunxi NAND driver validation

Executable TLA+ models and source-extracted C checks for the Linux Allwinner
NAND OOB series. The models concentrate on the interactions that are difficult
to review locally: logical ECC steps versus controller slots, packed OOB
layouts, DMA batching, error/pattern classification, interrupt completion reuse,
and failure cleanup. The extended suite also covers the DMA backends, command
parser, SRAM/media identity, NAND-core contracts, shared-controller lifecycle,
probe unwinding, and timing calculations.

The reference is the [posted v9 series, 18 patches](https://lore.kernel.org/r/20261017-submit-sunxi-nand-vendor-oob-layout-v1-v9-0-b624c09e3ee5@gmail.com).
The v10 draft inspected when this repository was created has the same driver
implementation. This repository validates that pinned implementation, **not
whatever happens to be in the latest kernel tree**.

## Run

Requirements: Python 3.9+, Java 17+ (the CI template uses 21), and GCC with its
matching `gcov`, AddressSanitizer and UndefinedBehaviorSanitizer. No board, root
privileges, or kernel build is needed. The tests do not access NAND devices.

```sh
python3 run.py --download
python3 run_c.py
python3 run_lowlevel.py
```

`--download` fetches the official TLA+ tools 1.8.0 jar and verifies its pinned
SHA-256. To use an existing copy:

```sh
python3 run.py --jar /path/to/tla2tools.jar
python3 run.py --list
python3 run.py --case irq --jar /path/to/tla2tools.jar
```

Results, generated TLC configurations, counterexample traces, extracted C,
compiler diagnostics, and machine-readable summaries go in ignored `results/`
subdirectories. A timeout, syntax error, unexpected invariant violation, or
sanitizer failure fails the run. Expected counterexamples must violate their
specific named invariant; an arbitrary TLC failure does not count as success.

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

The two C harnesses extract 82 function bodies **unchanged** from the bundled
submitted driver, verify its hash, then compile them with host-side fixtures.
The low-level harness also executes the actual NAND-core operation parser and
suboperation helpers from the same revision. They supplement the models;
neither is a full NAND controller emulator or kernel integration test.

The extended suite has 195 TLC configurations, including positive checks,
deliberate mutations, hardware/core assumption tests and source-contract
counterexamples. See [the recorded validation](VALIDATION.md),
[the seven-area coverage map](docs/EXTENDED-COVERAGE.md),
[patch-by-patch coverage](docs/COVERAGE.md),
[source-level findings](docs/FINDINGS.md), and
[assumptions and limits](docs/ASSUMPTIONS.md).

Each C runner also makes a separate, unsanitized GCOV build. To combine their
source-mapped coverage reports, substitute the result directories printed by
the runners:

```sh
python3 coverage_report.py results/c-RUN/summary.json results/lowlevel-RUN/summary.json
```

This reports unexecuted functions and missed source lines explicitly. GCOV
coverage of extracted functions under host fixtures is not whole-kernel branch
coverage; TLC action coverage is a separate measure.

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
and binding, plus unchanged NAND-core parser excerpts, with original licensing.
Source functions and their hashes are listed in each C-run summary. The
reference files are inputs, not a second driver to edit.

When changing the reference revision, replace it from the actual submitted
series, update `source.json`, review the function-to-model mapping, and rerun all
three runners. Do not update a digest merely to make a mismatch disappear.
Changes to hardware assumptions require independent documentation or
measurements, not just a model that produces the desired result.

## Optional CI

[ci/validate.yml](ci/validate.yml) is a pinned GitHub Actions workflow template.
It runs all three suites on Ubuntu 24.04 with Java 21 and retains diagnostics and
counterexample traces. It is intentionally outside `.github/workflows/` in this
initial publication because the publishing credential cannot install workflows.
Automatic CI is not enabled. Install it as `.github/workflows/validate.yml` using
a workflow-authorized credential to activate it.

Repository-authored code is GPL-2.0-or-later. See `LICENSE`; the reference binding
retains its dual-license SPDX notice, and the NAND-core excerpts retain their
GPL-2.0-only notices.
