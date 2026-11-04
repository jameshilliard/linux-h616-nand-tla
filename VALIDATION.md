# Initial validation record

Validated on 2026-11-03 against the posted 18-patch NAND OOB v9 implementation.
The locally inspected v10 draft had identical driver/binding contents. Reference
digests and the exact sent commit are in [source.json](source.json).

## TLC

TLC 1.8.0, one worker, seed 1, fingerprint polynomial 0, explicit-state checking.
All 68 configurations matched their expectations:

- 48 positive configurations: invariants and termination properties passed.
- 19 mutation configurations: each produced its expected invariant violation.
- 1 assumption configuration: ineffective DMA abort produced `NoUnsafeUnmap`.
- All required positive-model actions were reached; no uncovered required action.

Positive configurations generated 124,384 states and found 113,822 distinct
states, summed across separate configurations. Including negative tests, the
corresponding sums were 133,578 and 122,659. These are finite-instance counts,
not a proof for unbounded geometries or executions.

## Source-extracted C

56 unchanged function bodies from the pinned driver compiled with GCC 15.2.0,
`-Wall -Wextra -Werror`, AddressSanitizer, UndefinedBehaviorSanitizer, and the
kernel-style overflow/aliasing flags recorded by `run_c.py`.

| Check | Result |
| --- | --- |
| Layout, user-data allocation, DMA batching and packed length codes | 614,118 admitted layout cases passed |
| Randomizer | All 65,536 input seed values at advances 0/1/8; both full ECC seed tables; byte transforms and BBM symmetry passed |
| Firmware erased-spare predicates | 921,600 truth-table/boundary cases passed |
| Generic ECC patterns/errors | 6,912 combinations passed |
| Vendor ECC classification and decoded OOB | 3,456 combinations passed |
| Unprotected OOB coalescing, representation and transfer faults | 612 cases passed |
| Page/subpage/OOB writes, single-fault injection | 412 executions passed |
| Page/subpage/OOB reads, single-fault injection | 456 executions passed; both ECC counters rolled back on failed DMA reads |
| ECC initialization | 4,872 hardware-ECC configurations accepted, 11,200 rejected, 56 left for core software fallback; all assertions passed |
| Over-limit geometries within that initialization matrix | 3,600 rejected |
| Additional boundary checks | Small-page column commands, tail cursor, raw-reread errors, zero-valued pattern-ID cache, counter-word reuse, 16/32-step bitmap boundaries passed |

No sanitizer failures or compiler diagnostics remained in the final run.
Local tools were Python 3.14.4 and OpenJDK 25.0.4.1. The provided CI template
targets Ubuntu 24.04 with Java 21, but hosted CI has not run: activating the
workflow requires a workflow-authorized publishing credential.

## Scope of this result

No kernel implementation was changed by this validation, and no new hardware
test was performed. The unresolved successful-DMA-stop assumption, media
classification limitations, core contracts, and other coverage boundaries are
listed in [docs/ASSUMPTIONS.md](docs/ASSUMPTIONS.md).

Rerun the scripts when changing the reference or models (or use CI once it is
enabled). The runners record source/model hashes in their machine-readable
summaries; this document records the initial checked version, not future runs.

# Extended validation record

Validated on 2026-11-03 against the same pinned v9 source. The initial record
above is retained as history; the following supersedes its coverage totals.
No production driver/binding change or hardware test was made.

## Extended TLC run

All **195 configurations across 13 models** matched their expectations:

- 143 positive configurations: invariants and termination passed under the
  configured contracts and fairness assumptions.
- 42 deliberate mutations: each failed its specifically named invariant.
- 8 assumption tests: each reproduced the expected consequence of removing
  a hardware/core guarantee.
- 2 source-contract tests: split address count and failed external DMA
  configuration reproduced their specific invariant failures.
- All 118 required positive-model actions were reached across the suite.

Positive cases generated 284,711 states and found 218,086 distinct states.
Across all cases the sums were 299,786 generated and 230,831 distinct states.
These are sums over separate finite configurations, not one unbounded proof.
TLC settings and the pinned jar remain those of the initial run.

| Model | Positive | Mutation | Assumption / source-contract |
| --- | ---: | ---: | ---: |
| NandBatch | 12 | 5 | 0 |
| NandOob | 9 | 2 | 0 |
| NandIrq | 3 | 3 | 0 |
| NandTransfer | 2 | 5 | 1 |
| NandEcc | 22 | 4 | 0 |
| NandController | 6 | 3 | 4 |
| NandDma | 17 | 4 | 0 |
| NandCommand | 8 | 4 | 1 |
| NandMedia | 4 | 3 | 0 |
| NandCore | 10 | 4 | 0 |
| NandLifecycle | 9 | 2 | 2 |
| NandProbe | 28 | 2 | 1 |
| NandTiming | 13 | 1 | 1 |

The action parser now counts an action that executes but produces only
already-known successor states. This does not loosen invariant or liveness
checking; it fixes the interpretation of TLC's distinct/total coverage counts.

## Extended source execution

Both C suites passed with GCC 15.2.0, `-Wall -Wextra -Werror`, ASan and UBSan.
They extract 82 unique unchanged driver functions (59 in the OOB harness,
25 in the low-level harness, two shared), plus unchanged NAND-core parser
functions and definitions. The driver and parser excerpts were compared with
the exact sent source commit, not only with the local digest file.

The initial C matrices remain passing. Additional checks passed:

| Check | Result |
| --- | --- |
| Generic OOB finish | 72 tail/cursor/representation/error combinations |
| Attach/format/allocation policy | 192 combinations; whole-page vendor subread and actual 32-bit ECC counter wrap |
| Protected metadata identity | 152 distinct-byte page/layout/format cases; BBM, parity and tail sentinels |
| DMA prepare/cleanup | 216 backend/direction/size/alignment/failure cases; separate abort-contract witnesses |
| DMA initialization | 18 mask/allocation/channel/defer/configuration cases |
| IRQ/poll | 280 status/mask/timeout cases; W1C preservation and partial-event draining |
| Raw SRAM/media | 88 multi-page round-trip and partial-program/failure cases; additional FIFO-error exits |
| Direct command execution | 38 address/direction/split-data cases; additional invalid command and readiness-error tests |
| Actual NAND-core parser plus driver exec_op | 32 read/write/check-only/RB/error cases; additional split-address and unsupported-instruction tests |
| Timing lookup | 960,120 independent-oracle cases |
| Complete interface arithmetic | 2,000 constraint/EDO/check-only cases; invalid/failing clock-round tests |
| Chip selection | Alternating geometry/timing/clock/chip-select contexts; ignored rate-change error and skipped retry reproduced |

Separate unsanitized GCOV builds passed too. Combined source-mapped results:

- 90 function definitions in the pinned driver; 82 extracted and all 82 executed.
- 1,268 / 1,291 GCOV lines in those extracted functions reached.
- 601 / 644 branch edges in those host-compiled functions taken.

The eight unextracted functions and remaining source branches are documented in
[EXTENDED-COVERAGE.md](docs/EXTENDED-COVERAGE.md). Fixture macros affect branch
accounting, so these are not whole-driver or in-kernel coverage percentages.

## Findings and publication limits

Three conditional source-level failures have actual-source reproducers; see
[FINDINGS.md](docs/FINDINGS.md). No supported-device runtime reproduction or
kernel fix is claimed. Successful DMA stop, command/DMA event ordering, PIO
quiescence, clock behavior and core lifetime remain explicit contracts.

Public files were checked for local paths and private test-hardware details.
The inactive CI template includes both C suites and the full model run. Hosted
CI is still not enabled with the available publishing credential.

# Integration validation record

Validated on 2026-11-03 against the same pinned v9 implementation and its
matching MTD/NAND-core/BBT sources. The preceding records remain historical;
this extension adds the six areas in
[INTEGRATION-COVERAGE.md](docs/INTEGRATION-COVERAGE.md).

## Reproducible TLC pin

The earlier records used a TLC build labeled 1.8.0 with SHA-256
`fc6878c603f0fdc18b9fe3e5bf51860cb90ab5b1154e3c371660c036d6bdd63e`.
During clean-download validation, that digest did not match the asset served
by the recorded v1.8.0 prerelease URL. The previous metadata therefore did not
provide a working fresh-download reproduction.

The current pin is the publicly downloaded v1.8.0 prerelease asset
`706603562`, build `2026.11.03.180953`, SHA-256
`e20447644bfabd29d6bd0fc2c0eb1473fb301c1a0e4dfe1961938ce5a191edbf`.
The official release metadata and downloaded bytes agreed. All configurations
were rerun with this exact jar, not merely relabeled. A future upstream asset
replacement must fail digest verification. The original digest is retained in
`source.json` for auditability.

## Integration TLC result

All **276 configurations across 20 models** matched their expectations:

- 205 positive configurations passed their invariants and termination property.
- 52 deliberate mutations failed the specified invariant.
- 13 assumption tests reproduced the specified safety or liveness failure.
- 6 source-contract configurations reproduced the named failure in the modeled
  implementation. These are configurations, not six newly discovered bugs.
- All **174 required positive-model actions** were reached; none was uncovered.

Positive configurations generated **347,012 states** and found **245,746
distinct states**. Across positive and negative configurations the respective
sums were **362,912** and **259,066**. These sums describe separate bounded
instances, not an unbounded proof. The run used one worker, seed 1 and
fingerprint polynomial 0, and took approximately 233 seconds locally.

| Added model | Positive | Mutation | Assumption / source-contract |
| --- | ---: | ---: | ---: |
| NandRequest | 15 | 1 | 2 |
| NandBbt | 4 | 1 | 3 |
| NandFormat | 19 | 1 | 0 |
| NandOwnership | 12 | 4 | 1 |
| NandPersistence | 8 | 1 | 1 |
| NandRt | 1 | 1 | 1 |
| NandProgram | 3 | 1 | 1 |

The RT starvation test must fail `Terminates` specifically. An unrelated TLC
error, unexpected invariant violation, timeout or missing counterexample is
not accepted as its expected result.

## Integration source execution

All three C harnesses passed ASan/UBSan and their separate GCOV executions:

| Check | Result |
| --- | --- |
| Existing driver matrices | All earlier layout/ECC/control/parser/timing assertions remain passing |
| Stored-image format oracle | 168 matching-format PIO/DMA writer-reader combinations, including distinct main/protected OOB, BBM, parity representations, tails and page-seed boundaries |
| SoC/backend DMA matrix | 200 source-capability-based preparation/direction/alignment/failure cases; MMIO whitelist and external-DMA bit preservation checked |
| DMA initialization and IRQ matrices | 18 init and 280 IRQ/poll cases remain passing |
| Core/MTD/BBT execution | 46 unchanged functions, all executed; 19,422 assertions passed |
| Core witnesses | Read-retry OOB duplication and ignored BBT scan/read transport errors reproduced in exact source |
| Persistent BBT | Actual mirror repair, reserved-region handling, no-marker mode, relocation and interrupted update across version rollover checked |

The backend count changed from 216 to 200 because the A10/A23 matrix now uses
their actual 16-step limit instead of also testing impossible 32-step inputs.
H616's 32-step cases remain. Driver-source coverage is unchanged: **82 of 90
functions**, **1,268 / 1,291 lines**, **601 / 644 branch edges**. Core-source
coverage is reported separately: **46 extracted/executed functions**,
**788 / 898 lines**, **371 / 501 branch edges**. No 100% source-branch or
whole-kernel coverage is implied.

All six added source/header files were compared byte-for-byte with the sent
kernel commit, independently of their local hash manifest. The source files
and kernel working tree were not modified.
Authored-file whitespace checks passed. Two existing trailing spaces in the
verbatim `mtdcore.c` input were retained to preserve source identity.

## Validation-design corrections and limits

The complete mutation run exposed an initially weak `RetryStats` assertion:
it checked the counters only when the result was already success, allowing
leaked counters to produce a false ECC result without failing the assertion.
It now also requires success for the model's guaranteed-successful second
attempt. The deliberate mutation fails as intended; the underlying C test
already checked this behavior. This was a validation fix, not a kernel fix.

Other construction checks caught a mismatched symbolic record/string
comparison and an incorrect expected TLC temporal-error message before the
final run. They were corrected without weakening the checked properties.

Three new conditional core findings are documented alongside the three earlier
driver findings in [FINDINGS.md](docs/FINDINGS.md). No claim is made that the
sunxi OOB patches introduced the core issues. Torn-write detection, hardware
DMA stop, controller seed reload, device-specific programming rules and IRQ
schedulability remain explicit contracts. Hardware BCH encoding, analog power
loss, real memory ordering and complete kernel integration are not proved.

The applicable general, false-positive, locking, I/O, alignment and allocation
Sashiko prompts were checked as documented in the integration coverage map.
No production driver change, destructive NAND operation, or new hardware test
was performed. Public files were checked for private paths/hardware details.
The CI template includes the core runner but remains inactive until installed
with a workflow-authorized credential.
