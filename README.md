# sunxi NAND OOB validation

Executable TLA+ models and source-extracted C checks for the Linux Allwinner
NAND OOB series. The models concentrate on the interactions that are difficult
to review locally: logical ECC steps versus controller slots, packed OOB
layouts, DMA batching, error/pattern classification, interrupt completion reuse,
and failure cleanup.

The reference is the [posted v9 series, 18 patches](https://lore.kernel.org/r/20261017-submit-sunxi-nand-vendor-oob-layout-v1-v9-0-b624c09e3ee5@gmail.com).
The v10 draft inspected when this repository was created has the same driver
implementation. This repository validates that pinned implementation, **not
whatever happens to be in the latest kernel tree**.

## Run

Requirements: Python 3.9+, Java 17+ (the CI template uses 21), and GCC with AddressSanitizer
and UndefinedBehaviorSanitizer. No board, root privileges, or kernel build is
needed. The tests do not access NAND devices.

```sh
python3 run.py --download
python3 run_c.py
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

The C harness extracts function bodies **unchanged** from the bundled submitted
driver, verifies its hash, then compiles them with small host-side fixtures. It
checks actual layout and initialization calculations, randomizer transforms,
erased-page predicates, ECC snapshots, generic/vendor reads, and page/subpage/OOB
control flow under injected transport failures. It supplements the models; it
is not a NAND controller emulator.

The initial validation has 68 TLC configurations: 48 positive checks, 19
deliberately broken variants that must produce counterexamples, and one explicit
DMA-stop assumption test. See [the recorded validation](VALIDATION.md),
[patch-by-patch coverage](docs/COVERAGE.md), and
[assumptions and limits](docs/ASSUMPTIONS.md).

## Interpreting the result

Passing is bounded evidence about these models and these C fixtures. It is not
a proof of all executions of the Linux driver, hardware correctness, NAND media
reliability, or complete kernel integration. There is no mechanically verified
C-to-TLA+ refinement mapping.

In particular, cleanup relies on reset/termination actually stopping DMA. The
negative assumption test demonstrates why this matters; it is **not a confirmed
hardware failure or a claim that the submission introduces that behavior**.
The repository documents other unmodeled areas explicitly.

## Source provenance and maintenance

`source.json` records the posted message ID, base, sent tip, driver digest, and
TLC version/digest. `reference/` contains the exact submitted driver and binding,
with their original licensing. Source functions and their hashes are listed in
each C-run summary. The reference files are inputs, not a second driver to edit.

When changing the reference revision, replace it from the actual submitted
series, update `source.json`, review the function-to-model mapping, and rerun both
suites. Do not update a digest merely to make a mismatch disappear. Changes to
hardware assumptions require independent documentation or measurements, not
just a model that produces the desired result.

## Optional CI

[ci/validate.yml](ci/validate.yml) is a pinned GitHub Actions workflow template.
It runs both suites on Ubuntu 24.04 with Java 21 and retains diagnostics and
counterexample traces. It is intentionally outside `.github/workflows/` in this
initial publication because the publishing credential cannot install workflows.
Automatic CI is not enabled. Install it as `.github/workflows/validate.yml` using
a workflow-authorized credential to activate it.

Repository-authored code is GPL-2.0-or-later. See `LICENSE`; the reference binding
retains its dual-license SPDX notice.
