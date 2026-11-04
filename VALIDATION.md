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

# Deep validation record

Validated on 2026-11-03 against the same pinned OOB v9 implementation. The
preceding records remain historical. This extension covers all six follow-up
areas in [DEEP-VALIDATION.md](docs/DEEP-VALIDATION.md): discovery and cleanup,
backend recovery, bootloader/ONFI timing handoff, cross-version formats, real
BCH/randomizer composition, and compound read/BBT failures.

## Full TLC regression

All **356 configurations across 23 models** matched their expectations:

- 260 positive configurations passed their invariants and termination property.
- 54 deliberately broken implementations failed the specified invariant.
- 32 assumption tests reproduced the specified safety or liveness failure.
- 10 source-contract cases reproduced their named failures; this is not a count
  of ten distinct bugs.
- All **199 required positive-model actions** were reached; none was missing.

Positive cases generated **348,300 states** and found **246,695 distinct
states**. Across all cases the respective sums were **365,233** and **260,690**.
These are sums across bounded configurations. The complete run took about
282 seconds with the pinned TLC jar, one worker, seed 1 and fingerprint
polynomial 0. Every copied model used for a case was compared byte-for-byte
with the final model, as well as checking the summary's model hashes.

| New model | Positive | Mutation | Assumption / source-contract |
| --- | ---: | ---: | ---: |
| NandRecovery | 5 | 1 | 18 |
| NandNegotiation | 31 | 0 | 2 |
| NandRelocation | 16 | 1 | 1 |

Five additional `NandRequest` configurations cover successful retry followed
by transport failure: three main-data checks and two OOB error-prefix witnesses.
The recovery model distinguishes stopping the DMA producer, retiring pending
bus writes, stopping the NAND command, suppressing late events, and draining
the IRQ. No single stop-register write is treated as proof of all five.

## Source execution and independent format checks

All source-extraction suites passed their sanitizer and separate GCOV runs.
The codec/library and image-builder tests passed ASan/UBSan too. Earlier
layout, parser, ECC, IRQ, initialization and arithmetic matrices remain passing.

| Added or extended check | Result |
| --- | --- |
| Discovery, attach, registration, cleanup and ONFI negotiation | 20 unchanged driver/core bodies; 1,011 fixture cases and 65,973 assertions |
| Provider configuration/termination | Four unchanged sun4i/sun6i entrypoints; 64 cases; all 52 compiled lines and 12 branch edges reached |
| Pre-series Linux/BSP compatibility | 6,864 layout, seed and bidirectional user-data/register-packing cases |
| Independent BCH parity | 120 vectors spanning all 15 driver strengths, 512/1024-byte messages and 0/4/16/32 protected bytes |
| BCH errors | 600 correction/over-capacity cases; all selected in-capacity errors corrected, all 120 selected over-capacity witnesses rejected |
| Independent sunxi-tools page reconstruction | 84 complete images, including randomizer seed boundaries |
| Real BCH composed with exact driver helpers | 120 binary profiles, 204 PIO/DMA round trips, and 80 wrong-seed witnesses |
| Compound requests and BBT replacement | Ten retry/transport and erase/program/exhaustion/restart cases; core suite now passes 19,592 assertions |

The BCH result does **not** promise detection of all errors beyond capacity.
Unused parity padding bits are excluded from correctable-symbol counts. The
image-builder comparison respects its post-encoding BBM overwrite; it does not
incorrectly equate that format with Linux's pre-encoding plain-BBM transform or
the BSP's packed normal-page metadata. No captured silicon-generated golden
image was used.

Combined unchanged-driver-body coverage is now:

- **90 / 90 functions extracted and executed**.
- **1,401 / 1,424 compiled source lines** reached.
- **667 / 708 compiled branch edges** taken.

These are host-fixture measurements, not whole-kernel coverage. Twenty-three
lines and 41 branch edges remain uncovered; most missed lines are timing
constraint rejections, with other OOB validation/error exits. The core request
suite separately reports **791 / 898 lines** and **374 / 501 branch edges**.
The lifecycle/core harness reports **442 / 485 lines** and **206 / 258 edges**;
these overlap other harnesses and must not be added to the driver total.

## Audit corrections, findings and limits

The source-identity audit compared the added kernel and sunxi-tools inputs with
their exact Git objects, and the BSP inputs with their pinned public snapshot.
It caught one extra terminal blank line in the imported `rawnand_chip.c`; the
reference and digest were corrected to the public bytes, and the codec/driver
compatibility suite was rerun. No extracted function or spare-capacity
expression changed. Original reference whitespace is otherwise preserved.

While constructing the tests, error-position selection was corrected to avoid
duplicate flips and BCH padding bits. The composed driver profiles were also
restricted to admitted 512-byte single-step and larger 1-KiB-step geometries,
with legacy strengths capped at 64. These were fixture/oracle corrections,
not kernel changes or weakened success assertions.

One new conditional source finding is recorded: timing-mode mismatch recovery
can return success after a failed mode-0 setup or RESET. The actual core body
and two model cases reproduce it; hardware reachability is not established.
The compound read tests extend the existing retry/OOB finding rather than
adding another distinct bug. The provider audit narrows the earlier ignored
DMA-configuration error: the pinned sun4i and sun6i callbacks always return
zero, so that injected error is not reachable through those implementations.
See [FINDINGS.md](docs/FINDINGS.md) for all seven conditional observations.

The applicable Sashiko guidance and historical review sources are listed in
the deep-validation map. Remaining contracts include actual bus retirement,
late hardware-event suppression, kernel-service concurrency, hardware BCH and
randomizer behavior, and torn-write detection. Neither formal models nor host
fixtures establish those physical guarantees.

No production kernel changes, NAND writes or new hardware tests were performed.
Public artifacts were checked for private paths and test-hardware details.
The updated seven-suite GitHub Actions template remains inactive.

# Boundary and historical-regression validation record

Validated on 2026-11-03 against the same pinned OOB v9 implementation. Earlier
records above are preserved. The six areas are mapped in
[BOUNDARY-VALIDATION.md](docs/BOUNDARY-VALIDATION.md), including the source and
fixture boundary of each test.

## TLC regression

All **408 configurations across 28 models** matched their expectations:

- 301 positive configurations passed their invariants and termination property.
- 58 mutations failed the specifically required invariant.
- 35 assumption tests reproduced their named safety/liveness failure.
- 14 source-contract cases reproduced their named failure. These are test
  configurations, not 14 distinct bugs or 14 corrected problems.
- All **226 required positive-model actions** were reached.

The additions are `NandSequence`, `NandMtdLifetime`, `NandProvider`,
`NandDiscovery` and `NandProviderBanks`. The provider model explicitly separates
the NAND client's no-callback case from a generic callback using the DMA buffer.
The former does not acquire a fictitious callback-drain requirement.

The runner now snapshots model bytes before beginning the run and hashes the
snapshot actually checked. Changing a model in the workspace during a run can
no longer make the final digest silently describe different code. Final model
digests and each generated case copy were checked against the released files.

## Exact-source C and regression tests

All previous driver, low-level, core, lifecycle, provider-entrypoint and codec
suites passed again. All five new runners passed their declared expectations.
The production driver/reference bodies remain unchanged.

| New runner | Result |
| --- | --- |
| Core/driver composition | 30 persistent-media profiles, 192 explicit requests, 1,158 page/OOB adapter calls including BBT traffic; AUTO/PLACE/RAW, cross-chip, error and reuse checks passed |
| Discovery-to-attach | 483 ONFI/ID scenarios; ONFI attach submatrix has 576 accepted and 576 rejected profiles; CRC majority repair, extended ECC, malformed sections and three allocation failures checked |
| MTD lifetime | 36 schedules, including 12 expected open-reference/resource-cleanup witnesses; nested partitions and both partitioned-master configurations |
| Provider completion | 64 sun6i event schedules plus eight wrong-bank and four unvisited-bank witnesses; 24 sun4i schedules including two stale cyclic-callback witnesses; NULL/non-NULL callbacks and descriptor reuse checked |
| Historical mutations | Unmodified baseline passes; all four adapted reseed/OOB-column/subpage-callback regressions fail their specific assertions |

Separate unsanitized GCOV results, restricted to extracted source bodies:

| Runner | Extracted / executed functions reported | Lines hit / compiled | Branch edges hit / compiled |
| --- | ---: | ---: | ---: |
| Composed requests | 112 / 89 | 1,220 / 1,833 | 486 / 944 |
| Discovery core | 10 / 10 | 220 / 235 | 103 / 128 |
| MTD lifetime | 9 / 9 | 83 / 94 | 37 / 48 |
| Provider completion | 24 / 24 | 332 / 342 | 103 / 130 |

These reports overlap earlier suites and cannot be added as disjoint coverage.
The discovery runner also compiles the driver adapter's 59 source bodies; its
coverage row above reports the ten discovery-core functions only. The composed
report deliberately retains its uncalled functions instead of implying full
coverage. The earlier combined driver matrix still executes all 90 driver
function bodies, without claiming all branches or full-kernel execution.

ASan and UBSan checks passed subject to one explicit provider-runner exception:
only `vchan_complete()` has its null-member-address check disabled for the
pinned empty-cyclic-callback address expression. The source body is unchanged;
ASan and other UBSan checks remain. The rationale and its limitations are in
the boundary map. Historical mutation success requires the intended assertion
failure, not an unrelated sanitizer diagnostic or arbitrary crash.

## Source audit, observations and limits

The five added ONFI, partition and virt-dma inputs were compared byte-for-byte
with the pinned kernel Git objects, independently of their manifest hashes.
Source-execution summaries record extracted-body hashes, generated-unit hashes,
compiler version/options, diagnostics and explicit limitations. The four
historical fix commits were inspected; mutations are modern adaptations rather
than claims to execute those exact older trees.

Three additional source observations are recorded separately as findings 8–10:
MTD references do not defer sunxi resource cleanup; upper-bank sun6i DMA IRQ
dispatch can select the wrong channel or omit a partial bank; sun4i cyclic
termination can leave a stale callback descriptor. The last is outside NAND's
transfer modes. The user-draining assumption in earlier positive lifecycle
models is explicitly corrected to a caller obligation, not a core guarantee.
No newly demonstrated regression in the submitted OOB changes was found.

The applicable Sashiko general, technical-pattern, false-positive, callstack,
locking, I/O, allocation and alignment guidance was checked. Actual callers,
reference ownership, configuration branches and the absence of a NAND DMA
callback were inspected before classifying the observations. No bot approval
or complete kernel integration proof is claimed.

Remaining limits include actual device-core/sysfs concurrency, SMP provider
interleavings, physical DMA stop and bus retirement, manufacturer quirks,
hardware BCH/randomizer behavior and power-failure guarantees. No kernel
implementation was changed and no board or NAND operation was performed.
Public files were checked for private paths and hardware details. The
twelve-suite Actions template remains inactive until installed with an
appropriately authorized credential.

# Candidate-fix validation record

Validated on 2026-11-03 without replacing the pinned baseline. The local
kernel changes are organized as documented in
[CANDIDATE-FIXES.md](docs/CANDIDATE-FIXES.md); this record does not claim they
have been posted or merged.

The complete revised TLC suite passed **428/428 expectations across 29
models**: 313 positive configurations, 58 mutations, 35 assumption tests,
and 22 source-contract witnesses. All required positive-model actions were
reached. Added positive cases cover checked clock errors, checked timing
recovery, both current sun6i IRQ bank widths, and one-vchan sun4i
IRQ/tasklet/termination interleavings.

The modified baseline low-level, core, lifecycle and provider-boundary
runners were rerun successfully, retaining their original witnesses.
Separate candidate runs passed:

| Candidate source check | Result |
| --- | --- |
| sunxi split address / clock failure | All ten split address cycles emitted; failed clock setup leaves controller registers and cached rate unchanged, returns the error and allows a subsequent retry |
| NAND-core OOB / BBT | 20,648 assertions; final-attempt OOB identity, partial error lengths, marker-scan failure, primary/mirror and unversioned BBT read errors |
| NAND-core timing fallback | 1,011 cases, 65,972 assertions; controller/reset failures propagated, successful fallback preserved |
| Current sun6i IRQ handler and register helpers | 368 cases; zero wrong/missed events, versus 278 with the current unpatched baseline |
| sun4i callback lifetime | 60 schedules; zero freed callback uses, including repeated termination, stale IRQ lookup and termination from a callback |

ASan/UBSan and source-coverage checks passed with the previously documented
`vchan_complete()` null-member-address sanitizer exception. ARM64 `W=1`
object builds passed for `sunxi_nand.o`, `nand_base.o`, `nand_bbt.o`,
`sun6i-dma.o` and `sun4i-dma.o`. These are object builds, not a full kernel
build or a board test.

Review used the cached Sashiko general/core, technical-pattern,
false-positive, callstack, locking, I/O-accessor, allocation, alignment and
Fixes-tag guidance. Refreshing the public prompt location failed, so this
does not claim comparison against its latest revision.

The open-MTD-reference/unbind finding remains unresolved. The external DMA
configuration-error observation remains conditional because both supported
provider callbacks return zero. Physical bus retirement and cross-vchan
physical-channel reuse are not proved by the new one-vchan lifetime model.
No hardware operation was performed for these candidate checks.

# Independent core branches and current-driver audit

Validated on 2026-11-03. Earlier records remain historical results; the
current sunxi source and coverage boundaries are recorded in
[CURRENT-DRIVER.md](docs/CURRENT-DRIVER.md).

## Independent fixes

The four NAND-core patches were split into one-patch branches, each based
directly on `30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85` without the other
fixes or sunxi prerequisites:

| Branch | Patch commit |
| --- | --- |
| `submit/rawnand-read-retry-oob-v1` | `eb2ce5869ad103db21e25471c6485148990d0365` |
| `submit/rawnand-bbt-scan-error-v1` | `8e66378be96a4aedb0e8ead1f613bfbb3bfcd028` |
| `submit/rawnand-bbt-read-error-v1` | `7b52969f0dd731da2eddc24d28249817709e301e` |
| `submit/rawnand-timing-recovery-v1` | `ecd6bb737d5f9d6c07f7d130c9b7aec0d0817fc4` |

Each branch contains one implementation commit and one internal b4
tracking commit. The exported message count is one, without a cover
message. Local b4 checks, dependency checks and recipient discovery
completed. No messages were sent. The combined branch remains unchanged
at `5ee8144849a949b308b4e9b5275ac9ef8de5634c`.

The split preserves patch IDs, full messages, authorship and the single
matching sign-off. Fixes references were rechecked against Git history.
`run_split_core.py` reads each independent Git revision and tests only its
intended fix, retaining witnesses for the other unfixed core defects.
All four passed their source-executed regression checks. This is stronger
than testing only the combined patch stack; it is not a full kernel build
or hardware test of each branch.

## Current sunxi checks

The current-driver options were added to the page, lifecycle, composed,
discovery, lifetime and real-codec runners; low-level tests already had
that option. Source summaries now identify the driver actually compiled.
The coverage combiner checks both full-source and function-body hashes.
A negative test confirmed that mixing a v9 report with v10 reports fails.
The codec runner snapshots candidate bytes before invoking its driver
subprocess, preventing a checkout change from invalidating provenance.

Candidate suites passed with ASan/UBSan and source-mapped GCOV:

| Area | Result |
| --- | --- |
| Three complementary driver runners | 90/90 functions executed; 1,427/1,431 lines and 701/712 branch edges reached |
| Write-path fault matrix | Expanded from 412 to 1,636 executions; optional OOB and plain/scrambled variants |
| Generic batch / vendor finish transport errors | 114 executions; errors propagate without out-of-range writes or premature vendor-statistics publication |
| Direct DMA-write selection errors | 16 page/OOB failure-and-retry cases |
| Concrete chip selection | 24 equal/different-rate, three-backend, two-select error/retry cases |
| Timing setup | 2,096 full-interface cases plus 960,120 lookup cases |
| DMA/PIO callback selection | 9 no-DMA/external/internal and small/large-page profiles |
| Real BCH / driver composition | 600 codec cases, 120 parity vectors, 84 sunxi-tools images and 204 driver round trips |
| Core/driver composition | 30 profiles, 192 requests and 1,158 page/OOB adapter calls |
| Discovery and lifecycle | 483 discovery scenarios and 1,011 lifecycle/ONFI cases |
| MTD lifetime | 36 schedules; all 12 expected retained-user/resource-cleanup witnesses remain |

The pinned baseline suites also passed again, preserving their documented
source/assumption witnesses. The four adapted historical regressions still
fail their specifically expected assertions. Adjacent provider checks retain
the previously documented function-local sanitizer exception; no such
exception is used for the sunxi candidate driver tests.

## Expanded formal checks

The complete suite passed **446/446 expected results across 30 models**:
325 positive configurations, 63 mutations, 36 assumption tests and 22
source-contract witnesses. Every required positive-model action was reached.
The recorded model snapshots match the checked-in model bytes.

The new `NandSelection` contributes twelve positive configurations, five
mutations and one provider-contract counterexample. Its positive runs
explored 5,020 distinct states summed across the finite configurations.
It explicitly models same-rate chip switches, check-only operations,
clock-cache consistency, both callers' error propagation, repeated failures
and successful retries. This is a bounded abstraction connected to actual
source tests, not an automatically proved refinement of the C driver.

## Review and outstanding limits

Review used cached Sashiko general/core, technical-pattern, false-positive,
callstack, locking, I/O-accessor, allocation, alignment, OF and Fixes-tag
guidance. The public prompt refresh failed, so this record does not claim
the latest prompt revision was obtained. No dedicated NAND prompt exists
in the inspected snapshot. No production code was changed solely to
silence a speculative review warning.

No additional confirmed driver bug was found in this extension. The known
MTD-user/unbind lifetime issue remains open. Physical DMA quiescence,
posted bus accesses, late controller events, clock-provider failure and
rounding behavior, and actual hardware ECC/randomizer behavior remain
explicit assumptions or validation gaps. No board or NAND operation was
performed, and no public repository push was made for this extension.

# Command, retry, lifetime and byte-ownership refinement record

Validated on 2026-11-03. The six selected follow-up areas are documented in
[REFINEMENT-VALIDATION.md](docs/REFINEMENT-VALIDATION.md). The original v9
snapshot and the current v10 driver retain their recorded hashes. No kernel
implementation was changed in this extension.

## Full TLC run

All **551 configurations across 34 models** matched their expectations:

- 403 positive configurations passed invariants and bounded termination.
- 71 deliberate mutations failed their specifically named invariant.
- 39 assumption tests reproduced their named safety/liveness failure.
- 38 source-contract cases reproduced the expected failure. These are test
  configurations, not 38 distinct bugs or fixes.
- All **268 required positive-model actions** were reached.

The four new models contribute 105 configurations:

| Model | Positive | Mutation | Assumption | Source-contract |
| --- | ---: | ---: | ---: | ---: |
| `NandRecoveryRegisters` | 24 | 1 | 2 | 4 |
| `NandDetachPolicy` | 16 | 2 | 0 | 9 |
| `NandDmaBytes` | 8 | 3 | 1 | 1 |
| `NandRetryProtocol` | 30 | 2 | 0 | 2 |

Positive cases generated 362,764 states and found 254,620 distinct states.
Across all configurations, the sums were 382,878 and 270,177 respectively.
These are sums over separate finite instances, not an unbounded proof. The
full run took approximately 412 seconds with the pinned TLC jar, one worker,
seed 1 and fingerprint polynomial 0. Its recorded model hashes were compared
with the final model files; none differed. Results are retained locally in
`results/tlc-0rnccbqb/`.

## Source execution

All twelve pinned-source C/codec/historical runners passed again, including
the new transport runner. The current v10 driver also passed the page,
low-level, lifecycle, composed, discovery, lifetime, transport and historical
checks. The core/vendor inputs remain pinned independently of that driver.

| Added check | Result |
| --- | --- |
| Real command stack under composed requests | Existing 30 profiles, 192 explicit requests and 1,158 adapter calls pass through real command/RAW helpers, parser, dispatch and sunxi operations |
| Command faults and reuse | 36 failed-command/successful-next-read cases, plus successful raw write/read, OOB column, check-only and ready/busy variants |
| Manufacturer callbacks under core retry | 42 configurations; expected setup/restore errors and returned data lengths; 20 retained staged-state and 14 retained active-state observations under the explicit chip-state contract |
| Standalone manufacturer commands | 14 setup failures, ten partial staged states and 18 failed-restoration checks; subsequent explicit restore verified |
| Core byte copies | 2,688 reads and 128 writes; alignment decisions, full payload and every caller-buffer guard checked |
| Reset/selection refinement | Twelve successful context-restoration cases, twelve failed-stop/unmap witnesses and one conditional late-event witness |
| MTD release, lookup and I/O | 36 partition schedules plus four two-chip schedules; 16 later-I/O and twelve later-open witnesses; final release removes the lookup entry |
| Historical regression sensitivity | Positive baseline passes; seven mutations each fail their specific assertion, on both v9 and v10 |

The three new historical controls cover strength rounding, the 512-byte ECC
block bit and NULL descriptor preparation. Original upstream fix commits were
checked in Git; these are semantic adaptations, not exact historical builds.

Separate unsanitized GCOV runs report:

| Suite | Extracted / executed bodies | Lines hit / compiled | Branch edges hit / compiled |
| --- | ---: | ---: | ---: |
| Connected transport, v9 | 149 / 120 | 1,610 / 2,414 | 670 / 1,295 |
| Connected transport, v10 | 149 / 120 | 1,614 / 2,421 | 672 / 1,299 |
| MTD lifetime, either driver | 13 / 13 | 119 / 137 | 48 / 72 |

Uncalled bodies remain in the connected report; it does not claim exhaustive
source coverage. The complementary v10 driver suites still execute all 90
driver functions, with 1,427/1,431 lines and 701/712 branch edges reached.
These overlapping reports must not be added together as disjoint coverage.

ASan/UBSan runs passed. The unchanged adjacent provider runner retains its
previously documented function-local null-member-address exclusion; no new
sanitizer exclusion was added. The manufacturer/internal-dispatch reference
files were compared byte-for-byte with their pinned Git objects, independently
of manifest checks. Public files were scanned for local paths/private data.

## Interpretation and review limits

The MTD results strengthen the existing unbind/resource-lifetime finding.
Gate/drain and deferred-release policies pass the bounded model but have not
been implemented in the kernel. Retry-state retention and failed-stop/late
event behavior remain conditional on explicit hardware contracts; they are
not newly demonstrated successful-read corruption or board failures.

Cached Sashiko general/core, technical-pattern, false-positive, callstack,
locking, I/O-accessor, allocation and alignment guidance was checked. The
public prompt refresh failed, so comparison with its latest revision is not
claimed. No dedicated NAND prompt was present in the inspected set.

The known MTD unbind issue, physical DMA/cache and bus-stop guarantees,
manufacturer parameter semantics, real kernel concurrency and hardware
BCH/randomizer validation remain open. No board/NAND operation, kernel
change, mailing-list submission or public push was performed. The CI template
now includes the thirteenth suite; hosted CI was not run.

# Manual-backed register and counter validation

Validated on 2026-11-03. This record supersedes the earlier direct-H616
above-1024-byte transfer results: those fixtures incorrectly equated SRAM
capacity with the normal-command transfer-count limit. The historical records
above are retained to make that correction explicit.

## TLC

All **591 configurations across 35 models** matched their expectations with
the pinned TLC jar:

- 429 positive configurations passed invariants and termination.
- 75 mutations failed the specified invariant.
- 39 assumption tests reproduced their specified consequence.
- 48 source-contract configurations reproduced their specified failure.
- All **278 required positive-model actions** were reached.

Positive configurations generated 365,135 states and found 255,881 distinct
states. Across all configurations the sums were 385,437 and 271,580. The run
took about 441 seconds, with one worker, seed 1 and fingerprint polynomial 0.
These are sums over bounded instances, not an unbounded proof.

The added register model has 16 positive profiles and four mutations. Twenty
additional count-boundary configurations separate ten source-contract failures
from ten model-only capped-chunk candidates. No kernel patch is implied by a
positive algorithm candidate.

## Source and hardware evidence

All twelve C runners passed again, including their existing sanitizer and
separate GCOV checks where provided. No sanitizer exclusion was added. The
additional transport count mode passed its expected-witness checks on both
the unchanged v9 reference and the exact v10 driver digest recorded in
[CURRENT-DRIVER.md](docs/CURRENT-DRIVER.md). The low-level and ordinary transport
suites also passed on both driver versions.

| Directed check | Result |
| --- | --- |
| Raw buffers with correct ten-bit counter | 72 admitted round-trip cases; twelve oversized direct-call witnesses |
| Full page-to-extra-OOB-to-MMIO composition | Read and write both request 1872 tail bytes, move 848 in the hardware fixture, return success |
| NDFC status versus MDMA descriptor status | Three-event W1C preserves read-only state; descriptor-status bits 0 and 31 clear independently |
| FIFO availability versus FSM busy | Actual FIFO helper can succeed while FSM busy remains asserted; no completion guarantee inferred |
| Normal parser splitting | Existing multi-command, read/write, readiness and error checks remain passing |
| Historical controls | Positive baseline plus all seven expected regression failures pass |

The large-OOB geometry is synthetic but accepted by actual attach code. It
establishes a reachable driver caller path under that geometry, not a known
affected physical NAND model. No new board operation was performed.

Both manual-file hashes and all three archived hardware-record hashes were
verified locally. The vendor controller file was fetched again from its pinned
public commit and matched the bundled SHA-256. The
[hardware evidence map](docs/HARDWARE-CONTRACTS.md) distinguishes documentation,
vendor usage, earlier observations and unverified hardware guarantees. Reset
retirement, late hardware events, physical large-tail behavior and full kernel
concurrency remain unresolved; the model does not manufacture those guarantees.

Cached Sashiko technical-pattern, false-positive, callstack, MMIO/accessor,
alignment and locking guidance was checked. No dedicated NAND prompt was
available. The count finding was traced through callers and admission rather
than reported solely from a hypothetical out-of-contract helper argument.
This was a model/test audit, not a new kernel-series build or Sashiko approval.

Public changes preserve the pinned reference source and exclude private paths,
credentials, device identifiers and raw NAND captures. The CI template includes
the new count mode; hosted CI remains inactive. Validated additions are now
published to the repository as requested, without waiting for separate push
instructions.

# Generation-specific NFC contracts and count-limit fix

Validated on 2026-11-04. This extension separates the A10, A23 and H616
hardware profiles and tests the normal-command count fix added to the v10
draft. It preserves the posted v9 source and its expected counterexamples.
The current draft and full driver digest are recorded in
[CURRENT-DRIVER.md](docs/CURRENT-DRIVER.md).

## Evidence and model changes

The [hardware contract table](docs/HARDWARE-CONTRACTS.md) distinguishes
external DMA, address/count-register DMA and descriptor DMA; register offsets,
ECC bitfields, user-data banks, page-block encodings and valid normal-command
counts are generation-specific. The pinned A10 and A23 BSP headers and read
paths were compared with source-extracted capabilities. The A10 manual only
provides the NFC overview; the A23 manual could not be downloaded. These gaps
are recorded rather than filled with H616 register semantics.

`NandCommand` now separates the driver's advertised SRAM size from the
hardware RAM0-bank bound and count encoding. Its positive cases constrain
both to the documented/valid 1024-byte range. A permissive backing array is
used only in old-source count witnesses, never as evidence of an 8 KiB bank.
`NandRegisterProtocol` checks older profiles with and without command-start
event clearing. Their source fixture does not rely on automatic clearing.

## Formal results

All **667 configurations across 35 models** matched their expected outcomes:

- 503 positive configurations passed invariants and bounded termination.
- 75 deliberate mutations failed their named invariant.
- 39 assumption checks reproduced their named consequence.
- 50 source-contract configurations reproduced their named failure.
- All **278 required positive-model actions** were reached.

Positive instances generated 366,442 states and found 256,922 distinct states.
Across all configurations those sums were 386,750 and 272,627. The run took
about 491 seconds with the pinned TLC jar, one worker, seed 1 and fingerprint
polynomial 0. The recorded model hashes match the final model files. These
are bounded checks, not an unbounded proof or an automatically verified
refinement of the C driver. Local results are in `results/tlc-jdud70n7/`.

## Source checks and builds

| Check | Outcome |
| --- | --- |
| Candidate direct-buffer transfers | 144 round trips across A10/A23/H616; full payload, chunk count, final column, RAM0/count bounds and sentinels checked |
| Later-command errors | 18 read/write cases across all three profiles and each of the first three commands; propagated timeout, randomizer cleanup and untouched suffix checked |
| Pinned-source direct buffers | 120 positive round trips and twelve expected oversized-H616 witnesses retained |
| Composed page/OOB boundary | All four candidate cases transfer the full tail; baseline H616 still moves 848 of 1872 bytes while legacy transfers all 1928 bytes |
| Candidate page/ECC/layout tests | Passed, including 1,636 write faults, 456 read faults and 114 OOB transport cases |
| Candidate lifecycle | 1,011 cases and 65,973 assertions passed; existing core timing witness retained |
| Candidate real-software-BCH composition | 600 correction/overcapacity cases, 120 independent parity cases, 84 sunxi-tools images and 204 PIO/DMA round trips passed |
| Connected transport, baseline and candidate | 30 profiles, 192 explicit requests, 1,158 adapter calls, retry/byte-copy cases and 36 command-fault/reuse cases passed |
| Candidate discovery and lifetime boundaries | 483 discovery scenarios and 576 accepted/576 rejected attach profiles; 36 partition and four two-chip lifetime schedules retain the expected unbind witnesses |
| Historical regression controls | Positive candidate passes; all seven deliberately reintroduced regressions fail their specific expected assertion |
| Kernel object | ARM32 LLVM and ARM64 GCC `W=1` builds passed |
| Patch checks | New count fix passes strict checkpatch; b4 local checks pass all 21 patches; no external prerequisites declared |

ASan/UBSan runs passed; no sanitizer exclusion was added. Separate GCOV
aggregation for the current full driver digest executes all 90 extracted
driver functions, reaching 1,427/1,431 source-mapped lines and 701/712 branch
edges. Existing uncovered paths and the distinction from real-kernel coverage
remain documented in the current-driver audit.

The new kernel patch is separate from the format extension, includes the
H616-introduction Fixes tag and James's sign-off, and leaves the existing
1 KiB controllers' chunking unchanged. It caps direct helpers as well as
sharing the same limit with the parser. It changes neither DMA batch sizes
nor ECC layout. All pre-existing patch contents and historical sent-series
changelogs were checked for preservation.

Review used Sashiko's cached technical-pattern, false-positive, callstack,
I/O-accessor and alignment guidance at
`d048f8742f6740c38833adf02e87901412d771ca`. No dedicated NAND prompt exists in
that set. The count finding was checked through actual attach, page, extra-OOB
and buffer callers, not solely against an arbitrary helper argument. These
checks do not claim a Sashiko bot run or approval.

## Remaining limits

The physical extent/aliasing of H616's advertised SRAM aperture and hidden
randomizer reload behavior between normal commands remain unmeasured. Older
CNT bit width/readback and automatic-clearing behavior remain only partially
documented. The tests therefore distinguish supported requests from register
encoding and test alternative clearing contracts explicitly.

The known MTD-user/unbind lifetime issue, physical DMA/bus retirement, late
hardware events, full kernel concurrency and physical ECC/randomizer behavior
remain unresolved. The synthetic large-OOB geometry is not asserted to be a
part tested on a board. No NAND write, erase, power cycle or board access was
performed. Public changes contain no private paths or raw NAND captures.

# Raw-stream, DMA-aperture and persistent-context extension

Validated on 2026-11-04. The three added models and their source correspondence
are described in [STREAM-APERTURE-CONTEXT.md](docs/STREAM-APERTURE-CONTEXT.md).
The reference remains the posted v9. The candidate remains the unchanged v10
driver at `ccdee2ee49862a1377d31f5c541ce9d5fab327df`, with SHA-256
`7e2525a0097a156a7a4e952a073c03ca19872f1c417a42387f15d5b85081a50a`.

## Formal results

All **765 configurations across 38 models** matched their expectations:

- 584 positive configurations passed invariants and bounded termination.
- 85 deliberate mutations failed their named invariant.
- 46 assumption checks reproduced their named consequence.
- 50 source-contract configurations reproduced their named failure.
- All **298 required positive-model actions** were reached.

The extension contributes 98 configurations: 81 positive, ten mutations and
seven assumption checks. No new source-contract failure is claimed. Positive
instances generated 368,268 states and found 258,667 distinct states; the sums
across all configurations were 388,722 and 274,518. The run took about 558
seconds with the pinned TLC jar, one worker, seed 1 and fingerprint polynomial
0. All 38 recorded model hashes match the final files. These remain bounded
instances, not an unbounded proof. Local results: `results/tlc-4_vkhpzs/`.

## Source execution

| Check | Outcome |
| --- | --- |
| Independent raw-stream bytes | 48 candidate sequences across A10/A23/H616; 32 baseline sequences across A10/A23; timeouts preserve the completed prefix and cleanup before changed-seed/plain requests |
| Hypothetical command-start reload | Three candidate/two baseline fixtures pass same-chunk round trips but differ from the independent physical image; explicitly a hardware-assumption witness |
| DMA aperture | 52 mapping-endpoint/direction/allocation cases per driver version; two artificial above-mask truncation witnesses retained separately |
| Persistent user-data length registers | 87 sequences per version plus legacy no-MMIO; packed-word boundaries, logical offsets, zero fields, stale unused slots and neighboring sentinels |
| Connected transport | Baseline and candidate ordinary transport and count-boundary modes pass; baseline retains the known H616 tail-count discrepancy, candidate transfers the entire tail |
| Candidate lifecycle | 1,011 cases and 65,973 assertions; existing core timing-recovery witness retained |
| Candidate BCH/source composition | 600 correction/overcapacity cases, 120 independent parity vectors, 84 sunxi-tools images and 204 PIO/DMA round trips |
| Candidate historical controls | Positive baseline and all seven named regression controls match their expectations |

The source checks use ASan/UBSan without adding an exclusion. The fresh
candidate page, low-level and lifecycle coverage union still executes all 90
extracted driver functions, reaching 1,427/1,431 source-mapped lines and
701/712 branch edges. The unchanged line/branch counts are expected: the new
checks add stronger values, boundaries and persistent-state sequences, not
previously unexecuted function bodies. Host-fixture coverage is not actual
kernel concurrency coverage.

Local result directories include `c-0daabmn9`, `lowlevel-jbningdv`,
`lifecycle-1zp3yvoz`, `transport-d4mcn48u`, `transport-count-umns4f18`,
`codec-cztdd94n` and `historical-kvup_k7c`. Baseline results include
`c-41ff1a76`, `lowlevel-0ndxes49`, `transport-qqqax58c` and
`transport-count-68ag51ai`, all under ignored `results/`.

## Review and limits

Source-contract review used cached Sashiko general technical-pattern,
false-positive, callstack, MMIO/accessor and alignment guidance at
`d048f8742f6740c38833adf02e87901412d771ca`; that set has no dedicated NAND
prompt. The mask, allocation and positive-size caller contracts were checked
against source initialization/callers and the DMA API. Arbitrary above-mask
or zero-size injections were not reported as reachable kernel bugs. Packed
length tests distinguish register-valid helper inputs from the allocator's
actual layouts. This is not a Sashiko bot run or a new full kernel-series review.

No new confirmed driver bug was established. The independent stream check
demonstrates why round-trip success alone cannot establish media compatibility;
it does not establish real silicon's command-reload behavior. Reset/bus
retirement, late events, MTD-unbind lifetime, physical SRAM behavior, full
kernel concurrency and C/model refinement remain outstanding. The prioritized
follow-up table records the evidence each needs. No kernel patches, board
state or NAND contents were changed. Public files exclude private paths,
credentials, identifiers and captures.

# Child-admission and probe-publication extension

Validated on 2026-11-04. Two added models and extended source fixtures examine
DT availability and scanning another NAND child after an MTD has been
published. The source paths, rejected false-positive explanations and
abstraction boundaries are in [PROBE-VISIBILITY.md](docs/PROBE-VISIBILITY.md).
The pinned v9 and v10 candidate source digests are unchanged.

## Formal results

All **836 configurations across 40 models** matched their expected outcomes:

- 649 positive configurations passed invariants and bounded termination.
- 85 deliberate mutations failed their named invariant.
- 46 assumption checks reproduced their named consequence.
- 56 source-contract configurations reproduced their named failure.
- All **311 required positive-model actions** were reached.

The new models contribute 71 configurations: 65 positive and six current
source-contract counterexamples. Positive instances generated 368,941 states
and found 259,234 distinct states; all configurations together generated
389,521 states and found 275,187 distinct states. The run took about 604 seconds
with the pinned TLC jar, one worker, seed 1 and fingerprint polynomial 0.
All 40 recorded model hashes match the final model files. Local full-run
results are in `results/tlc-0a7k0eky/`.

`NandChildAdmission` distinguishes missing/okay/ok status from unavailable
children and checks chip-select admission, presence and unwind. Its filtered
policy is model-only. `NandProbePublication` explores one to three children,
one bounded user operation, and scan/registration failure positions. Current
per-child publication can violate context ownership; deferred publication
passes those bounded checks without claiming to solve resource lifetime.

## Source results

Both unchanged driver versions passed the expanded low-level and lifecycle
host suites under ASan/UBSan, with no new sanitizer exclusions:

| Directed check | Result per driver version |
| --- | --- |
| DT availability | 28 status/presence/order cases; 16 unavailable-child scan witnesses |
| Probe versus live I/O entry | Real first-chip `nand_get_device()` leaves the controller mutex held while the next actual scan reaches detection |
| Wrong controller context | Twelve actual selector/exec/raw-buffer schedules launch with the second chip's CS/timing across A10/A23/H616 |
| Complete lifecycle runner | 1,040 cases, 69,882 assertions; prior timing-recovery witness retained |

The detector, MTD publication and mutex implementations remain explicit host
services. The real kernel MTD registration/open/read paths and the real
detector's RESET/READ ID calls were inspected separately. These are not kernel
SMP tests or physical NAND corruption measurements.

Baseline results are `results/lifecycle-j1_zih3w/` and
`results/lowlevel-5prakt6a/`; candidate results are
`results/lifecycle-g3hsn4_j/` and `results/lowlevel-171coumv/`.
Combining the new candidate reports with the unchanged page/ECC runner
`results/c-0daabmn9/` still executes all 90 driver functions and reaches
1,427/1,431 source-mapped lines and 701/712 branch edges. This adds meaningful
state/schedule checks without pretending that a function-hit count is
complete integration coverage. Python compilation and `git diff --check`
also passed.

## Findings, review and limits

Findings 12 and 13 are independent sunxi probe defects, present before the
OOB series and still present in both checked source versions. The second
requires separate NAND child nodes and can occur on a successful probe; it
does not depend on the known unbind/resource-lifetime problem. Availability
filtering and scan-before-publication are fix directions, not kernel patches
implemented by this update.

The review used cached Sashiko technical-pattern, false-positive, callstack,
OF, locking, I/O-accessor and cleanup guidance at
`d048f8742f6740c38833adf02e87901412d771ca`. No dedicated NAND prompt is present.
It explicitly checked downstream filtering, parent-device probe locking,
normal I/O locking, detection callers and cleanup. No Sashiko bot run or
approval is claimed. The overly broad controller-serialization statement in
the assumptions document now distinguishes normal I/O from unlocked scan.

Physical DMA retirement, late events, SRAM extent, randomizer reload,
whole-kernel concurrency and C/model refinement remain open. No kernel
repository change, board access or NAND write occurred. Public files retain
the pinned sources and exclude private paths, credentials, board identifiers
and raw captures. Hosted CI remains inactive; these are local validated runs.

# Standalone probe-fix validation

Validated on 2026-11-04 after implementing independent fixes for findings 12
and 13. Both candidates use NAND base
`30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85`; the OOB v10 branch is unchanged.
The pinned v9 reference, `source.json`, and all 40 model files are unchanged.

| Source | Lifecycle cases | Assertions | Unavailable-child witnesses | Identification while a published user holds the controller lock |
| --- | ---: | ---: | ---: | --- |
| NAND base | 1,213 | 82,070 | 16 | Reproduced |
| Available-child fix `c8a746138a9d` | 1,213 | 81,668 | 0 | Reproduced |
| Publication fix `f962731e99f6` | 1,209 | 81,952 | 16 | Prevented |
| Both fixes | 1,209 | 81,560 | 0 | Prevented |

All four positive configurations pass ASan/UBSan and their separate GCOV runs.
The newly added publication/unwind matrix contributes 173 cases per source,
covering zero to three children, fault injection at each event, and an
all-unavailable control. Earlier geometry, ONFI and allocation-failure tests
remain enabled. Different totals reflect which events are reachable under
the changed publication order, not removed checks.

Four negative controls pass only by reaching their required assertion:
unfiltered children, early registration, unregistering an unpublished chip,
and missing registration bookkeeping. Both real patches apply in either
order to yield identical trees. The combined driver SHA-256 is
`582d2f3ff57357c33f0d7f123a88a5418ba9b6e60277d3725687b5099e88b59c`.
The runner records all source digests and resolved commits in
`results/probe-fixes-iuklj9zl/summary.json`.

The unchanged pinned v9 source also passes the expanded baseline lifecycle
checks (1,213 cases, 82,070 assertions), retaining both expected witnesses.
Its local result is `results/lifecycle-knycat1z/`.

The two relevant TLA+ models were rerun: all 34 child-admission and 37
probe-publication expectations match, with all required positive actions
reached. Results are `results/tlc-amu22hhf/` and `results/tlc-tu_sjn4z/`.
The full 836-configuration run recorded above remains the latest full-suite
result; this update does not claim a second full-model run.

Both standalone kernel drivers pass ARM32 LLVM and ARM64 GCC `W=1` builds.
Strict checkpatch reports no errors, warnings or checks; each b4 export is
one signed-off patch with the original-driver Fixes tag and no external
prerequisites. No patches were emailed. Review used the general Sashiko
technical-pattern, false-positive and callstack guidance plus OF, cleanup,
locking and I/O-accessor prompts at the revision recorded above.

These are source-fixture and bounded-model results, not full-kernel concurrency
or silicon tests. The existing retained-MTD/unbind lifetime problem is outside
the fixes. No board access or NAND write occurred. Public validation additions
contain no private paths, credentials, device identifiers or raw captures.

# Probe fixes folded into sunxi v10

Validated on 2026-11-04 after incorporating findings 12 and 13 into
`submit/sunxi-nand-fixes-v10`, now at
`c95239dadd36c5d4c012c228ed4a95971c89b65a` on the unchanged NAND base
`30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85`. The draft has 23 patches:
availability filtering (`f79cdf7e993b`) and deferred publication
(`fa01a63067b1`) are patches 20 and 21, before the binding and format changes.
The standalone sunxi submission branches are archived; NAND-core and
DMA-provider fixes retain their independent branches.

The exact integrated driver SHA-256 is
`f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6`.
A fresh lifecycle run with both positive fix expectations passes 1,209 cases
and 81,560 assertions, including all 28 availability and 173 probe-unwind
cases, under ASan/UBSan and separate GCOV execution. Its local results are
`results/lifecycle-gpwetnbd/`. ARM32 LLVM and ARM64 GCC object builds pass at
`W=1` without warnings. No full-driver coverage total is inferred by mixing
this digest with older source reports.

The preservation audit checks all 21 original patch IDs, commit messages and
author identities, plus both added fixes, against their saved originals.
Three-way application of the original fixes onto the old final tree matches
the integrated tree exactly. The old cover-letter revision notes are preserved
verbatim and match the sent v9 history. Each patch has exactly one author
sign-off, while Assisted-by remains only in the cover letter. b4 local checks,
dependency checks and recipient discovery pass; export produces 24 messages
(cover plus 23 patches), with no external prerequisites. No mail was sent.

Review used the general Sashiko technical-pattern, false-positive and callstack
guidance plus OF, cleanup, locking and I/O-accessor prompts at
`d048f8742f6740c38833adf02e87901412d771ca`. This is prompt-based manual review,
not a Sashiko bot run. The retained-MTD/unbind lifetime finding remains outside
these fixes. The model files, pinned references and source manifest are
unchanged; the earlier TLC results remain the latest model runs. No board
access or NAND write occurred.

# Source-to-model probe trace conformance

Validated on 2026-11-04 against the unchanged pinned v9 driver and integrated
v10 driver SHA-256
`f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6`.
No kernel commit, pinned reference or source manifest changed in this extension.

`run_lifecycle.py --probe-traces` records scan, list, MTD publication and
cleanup observations from its actual extracted source execution. Each event
contains post-call list/publication snapshots. The trace runner verifies their
digest, all 57 input profiles, every fault index observed on each uninjected
path, and the occurrence of every observation kind for the candidate.
Sanitized and separate GCOV executions produce identical trace bytes.

| Source | Additional C probe cases | Distinct boundary projections | TLC result |
| --- | ---: | ---: | --- |
| Pinned v9 | 2,755 | 87 | The selected successful two-child trace reproduces `AllScannedBeforePublish`; other baseline traces are retained, not claimed to conform |
| Integrated v10 | 1,788 | 45 | Every projection passes all lifecycle protocol and snapshot invariants, including eventual trace completion |

The candidate's two positive batches and seven named negative controls pass
all nine expectations. Negative controls omit unregister/unlink, duplicate
insertion, alter a snapshot, access a disabled child, publish early or report
incomplete success. Each must violate its intended invariant. The baseline
witness passes its one expected-counterexample check. Source runs are
`results/lifecycle-wa5vjstx/` and `results/lifecycle-78v967xq/`; corresponding
TLC results are `results/tlc-naf7j6k3/` and `results/tlc-wxobwzkl/`.

The standard suite additionally includes five handwritten checker examples
and seven mutation controls, all 12 passing in `results/tlc-er9l4ye2/`.
These are protocol-checker unit tests, not the source-generated input records.
The existing publication and admission reruns pass 37 and 34 expectations
in `results/tlc-9_tfbbfa/` and `results/tlc-nnm94c4s/`. The independent probe-fix
runner also passes its four source configurations and four controls in
`results/probe-fixes-zvkzkrz5/`, preserving the ordinary non-tracing C checks.
Python compilation and whitespace checks pass. The standard inventory is now
848 configurations across 41 models; the previously recorded 836-configuration
run remains the latest full-suite run, not a new claim about all 848.

An initial unbatched constant containing every raw trace exceeded the TLC
process's 1 GiB heap and was correctly reported as a failure. The final runner
projects only observed protocol fields, deduplicates identical projections
with multiplicity retained, and checks at most 32 per wrapper. All 1,788
candidate executions are accounted for. No timeout, JVM error or parser error
is accepted as an invariant counterexample.

This is bounded trace conformance, not a whole-program refinement proof. The
observer does not alter any kernel function body, but MTD/OF/allocator services
remain host fixtures. It excludes retained users during cleanup, compound
faults, dynamic overlays and actual concurrent kernel execution. It neither
proves hardware quiescence nor fixes the retained-MTD/unbind problem. No new
kernel defect was established by these checks. Details and reproduction steps
are in [PROBE-TRACE-CONFORMANCE.md](docs/PROBE-TRACE-CONFORMANCE.md).
No hardware access or NAND write occurred, and public additions contain no
private paths, credentials, raw captures or device identifiers.

# Source-to-model DMA recovery trace conformance

Validated on 2026-11-04 against the pinned v9 driver and the same 23-patch
sunxi v10 draft at `c95239dadd36c5d4c012c228ed4a95971c89b65a`. No kernel
implementation, reference source or source manifest was changed by this work.

`run_lowlevel.py --recovery-traces` executes the unchanged preparation,
IRQ/wait, abort, cleanup and chip-selection helpers with boundary observers.
Each source version passes 264 additional schedules under ASan/UBSan and
separate GCOV execution: 168 normal-contract schedules and 96 deliberately
weakened-contract schedules. Both builds produce identical trace bytes, as do
the two driver revisions for this matrix. The shared trace SHA-256 is
`4f87dc0d2c2e59c17d02007ef7089afd6b391ae60798dea8ab84f331710a6cee`.
The final source runs are `results/lowlevel-xp3_ejex/` (v10) and
`results/lowlevel-_6edpw38/` (v9); their summaries record distinct driver and
function digests plus the exact fixture inputs. A non-tracing baseline rerun
also passes in `results/lowlevel-g6bhb41o/`.

The new `NandRecoveryTrace` checker accounts for all 264 inputs through 156
distinct boundary projections. The complete run passes **98/98 expectations**
in `results/tlc-oly56k92/`:

- 78 trace-integrity wrappers check every projection's ordering, snapshots,
  interrupt cleanup, mapping/direction accounting, context restoration and
  expected safety outcome. Each wrapper contains at most two projections.
- 12 conditional counterexamples reproduce their exact `MappingSafe` or
  `FreshCompletion` violation: failed stop, outstanding bus accesses after
  stop, old events after reuse and completion before memory retirement, on
  each of the three backends.
- Eight altered-trace controls fail their specific invariants for wrong
  snapshots/direction/context, missing unmap/abort, an incorrect timeout result,
  premature unmap and late restoration of the IRQ mask.

All required `Advance` action coverage is reached. Trace completion is checked
for each integrity wrapper; it is not a guarantee of hardware forward progress.
All 53 existing abstract recovery configurations also pass again in
`results/tlc-c96srjbe/`. These are targeted runs, not a new full-suite result.
The dedicated trace configurations remain separate from the standard catalog.
Python compilation, whitespace checks and parsing of the extended CI template
pass. No hosted CI execution is claimed.

An initial eight-projection wrapper exhausted TLC's 1 GiB heap and was correctly
reported as a failure. The final runner uses two-projection wrappers, preserving
all input multiplicities. `TLCCache` memoizes each immutable trace by index;
its mathematical definition is the identity operator. A focused cached and
uncached comparison produces the same 82 generated / 80 distinct states.
The final complete run has no parser, JVM, timeout or unexpected-invariant
failures. Neither a crash nor a different invariant counts as a successful
negative test.

The source fixtures still supply command launch, event delivery and physical
retirement behavior. The new model checks the resulting helper protocol; it
does not execute the whole page caller, prove DMA/bus quiescence, or establish
that the weakened contracts describe real silicon. No new kernel defect is
confirmed by this extension. The known retirement/event-causality, retained-MTD
lifetime and randomizer behavior limits remain explicit in
[RECOVERY-TRACE-CONFORMANCE.md](docs/RECOVERY-TRACE-CONFORMANCE.md).
No board access or flash write occurred. Public additions contain no private
paths, credentials, board identifiers or raw hardware captures.

# Source-to-model randomized OOB/ECC pipeline conformance

Validated on 2026-11-04 against the pinned v9 driver and the unchanged
23-patch v10 draft at `c95239dadd36c5d4c012c228ed4a95971c89b65a`.
Its driver SHA-256 remains
`f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6`.
No kernel implementation, pinned reference or source manifest changed.

Each driver passes 430 new source-helper executions under ASan/UBSan and
separate GCOV execution. Both builds and both source revisions produce the
same observation bytes. The matrix contains 607 raw-read calls, 52 transport
failures, 56 all-zero results and 164 erased-page results. An independent
Python reference checks complete captured/final OOB buffers, read regions,
statistics, return values and the main-buffer class. Source artifacts are
`results/oob-pipeline-by7jbrv3/` with `results/c-p69s06p9/` for v10 and
`results/oob-pipeline-lujdkwki/` with `results/c-ie93rner/` for v9.

- Shared input-vector SHA-256:
  `c1bd40bbad2e5adbcfb28aafa568717cd0db3f17b30f95b63b70b4bff5a2c65f`.
- Shared observation SHA-256:
  `1415a13a70612cb10d3001f3e7ffc7e481e446afb463d07ab6816dd2851868f8`.
- Candidate combined-fixture SHA-256:
  `7597dc75e931f7f0b490fb9655e477fb8d8f9b7d2fd696667c7c1ad550585741`.

The final `NandOobPipeline` run passes **119/119 expectations** in
`results/tlc-qv6os6x4/`: 108 wrappers cover every source record, and eleven
altered-observation/input controls violate their intended invariants. The
shared observations are checked once, not counted as two independent TLC
runs. All seven stage actions are reached. The checker independently defines
protected-byte geometry, maximal raw-read regions, re-encoded spare
classification, PIO/DMA representation differences and deferred accounting.
Input reuse verifies both the independent generator and the source-recorded
vector digest. These checks supplement rather than replace the C execution.

Targeted existing regressions pass **49/49** expectations selected by `oob`
in `results/tlc-rhp5j05c/` and **34/34** selected by `stream` in
`results/tlc-0vae9tn9/`. These are not full-suite reruns. The dedicated pipeline
runner remains separate from the standard configuration catalog.

The complementary candidate codec run passes in `results/codec-pjlw5_cw/`
with driver bridge results in `results/c-t9miou_h/`: 600 codec error cases,
120 independent driver/parity vectors, 84 sunxi-tools images, 204 source
round trips and 80 wrong-seed witnesses. The selected beyond-capacity matrix
rejects 120 cases with no observed miscorrection; it does not prove that
beyond-capacity errors can never miscorrect. These are software images, not
independently captured hardware golden images.

Development runs exposed TLC evaluator memory/time limits. The final model
memoizes immutable input/layout data and cursor-derived read bounds, then
binds each chosen region once before the byte update. Focused equivalent
formulations retain 43 generated / 39 distinct states for source wrapper 030
and 103 generated / 99 distinct states for wrapper 046. The final complete
run has no parser, JVM, timeout or unexpected-invariant failures. No such
failure is counted as a successful negative control. Python compilation,
whitespace checks and CI-template YAML parsing pass; hosted CI execution is
not claimed.

An archive of the staged public tree, excluding unrelated local MTD drafts,
also passes all 430 pinned-source traces and the selected capture-corruption
control. This checks that the released runner does not depend on those drafts;
it is a packaging smoke test, not another full TLC run.

The fixture supplies hardware-decoded USER_DATA, ECC/pattern status and raw
media bytes. It invokes the real batch/finish helpers through a hosted caller
loop, not the complete ECC page entrypoint. The directed inputs do not cover
the full Cartesian product, arbitrary C inputs, compound faults or actual
concurrent kernel execution. Synthetic parity in this matrix complements the
separate real-BCH checks. Hardware LFSR continuity/reload, physical BCH,
DMA retirement and retained-MTD/unbind lifetime remain unproved boundaries.
No new kernel defect was established. Details are in
[OOB-PIPELINE-CONFORMANCE.md](docs/OOB-PIPELINE-CONFORMANCE.md).
No board access or NAND write occurred; public additions contain no private
paths, credentials, board identifiers or raw hardware captures.

# Whole-request, file-lifetime and full v10 validation

Validated on 2026-11-04 against the 23-patch v10 draft at
`c95239dadd36c5d4c012c228ed4a95971c89b65a`, with driver SHA-256
`f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6`.
The production kernel and pinned posted driver are unchanged.

`run_suite.py` passes **61/61 jobs** in `results/suite-p4dxdl54/`. It copies
the candidate once, records the executable inputs and verifies that none
changed during the run. Every source runner, standard model group and dedicated
trace/ordering runner completes successfully. The standard catalog contributes
**875/875 expectations across 42 models**; the dedicated jobs add 278, for
**1,153/1,153 matched TLC outcomes**. Required positive action coverage is
complete. Expected known-bug and weakened-contract counterexamples remain
expected failures of their named properties, not claims that those issues
have been fixed.

| Dedicated check | Source observations | TLC expectations |
| --- | ---: | ---: |
| Probe/publication | 1,788 executions, 45 projections | 9/9 |
| DMA/IRQ recovery helpers | 264 executions, 156 projections | 98/98 |
| Randomized OOB/ECC pipeline | 430 executions | 119/119 |
| Whole requests and compound errors | 810 executions, 139 projections, 18 independently encoded stored images | 25/25 |
| Actual MTD open/close lifetime | 16 schedules, 32 opens/closes, four close/sync witnesses | 8/8 |
| Two-request IRQ/retirement interleavings | Bounded state exploration | 10/10 |
| Two-user NAND locking/unbind | Bounded state exploration | 9/9 |

The unchanged source bodies are tested under ASan/UBSan and a separate GCOV
build. Whole-request and file-lifetime artifact bytes must be identical in
both builds. The pre-existing provider-local UBSan exclusion remains documented
in the boundary guide; it is not silently represented as unrestricted sanitizer
coverage. Public MTD checks also pass 1,285,200 admission combinations, including
the deliberately retained generic-core boundary witnesses.

Combining only passed reports matching this driver and its function-body hashes
executes **90/90 functions**, **1,430/1,434 source-mapped lines** and
**706/718 branch edges**. No function is unextracted or uncalled. The four
unexecuted lines are timing-lookup error returns already excluded by preceding
admitted-range calculations. Remaining branch gaps include unsupported helper
combinations, parser-excluded cases, fixture error-pointer guards and the
unregister-warning branch. These are host-fixture coverage totals, not real
kernel branch coverage or proof of every path.

Key result directories are:

- Source/request: `results/request-7lnhi8_0/`; TLC: `results/tlc-huf0e0_j/`.
- Source/file lifetime: `results/fd-lifetime-45dxhdf2/`; TLC: `results/tlc-5i4wgsn1/`.
- Source/probe: `results/lifecycle-h0opktwa/`; TLC: `results/tlc-nx7mceif/`.
- Source/recovery: `results/lowlevel-_eh47eds/`; TLC: `results/tlc-ctoxxq_v/`.
- OOB pipeline: `results/oob-pipeline-7xzc2a77/`; TLC: `results/tlc-v1jgelp6/`.

Separate pinned-v9 runs also pass the new 810 request executions/25 TLC
expectations (`results/request-sh4gxp80/`, `results/tlc-nvtj8c44/`) and
16 file-lifetime schedules/eight expectations (`results/fd-lifetime-qddp2a4b/`,
`results/tlc-lx_15qhl/`). Candidate-only expectations are not applied to v9.

Development runs caught stale shared-fixture hook declarations and the new
registered-chip field missing from a lifetime fixture. They also caught a
double-applied 15-clock warm-up in the new independent request-vector generator;
the complete stored-byte comparison failed even though data-only reads passed.
Those validation defects were corrected, not converted to expected successes.
The final full run has no compilation, sanitizer, parser, JVM, timeout or
unexpected-invariant failures. Python compilation, whitespace checks and CI
template YAML parsing pass. No hosted GitHub Actions run is claimed.

An exported release archive also passes the 810 pinned-source request
executions, the double-confirm checker control, all eight file-lifetime TLC
expectations and the public MTD boundary runner. This packaging smoke test
uses only files in the release plus the pinned checker jar. Its local evidence
is retained under `results/release-smoke-Ly2GQs/`; it is not another full-suite
run and contains only synthetic test images.

No additional sunxi implementation defect is confirmed. Actual writable MTD
close reaches `nand_sync()` and its chip/controller locks after cleanup in the
retained-reference schedules, extending the existing lifetime finding. The
entry-gating and last-reference alternatives remain models, not kernel fixes.
Physical randomizer/BCH behavior, hidden stream reloads, SRAM extent, DMA/bus
retirement, late device events, real SMP/RT and device-core/devres ordering
remain explicit boundaries. No flash was erased, reformatted or written.
Details and reproduction instructions are in
[REQUEST-TRACE-CONFORMANCE.md](docs/REQUEST-TRACE-CONFORMANCE.md) and
[FD-LIFETIME-CONFORMANCE.md](docs/FD-LIFETIME-CONFORMANCE.md).

# Real H616 measurements and measured-stream validation

On 2026-11-04 a restricted external diagnostic module exercised an H616 with
Toshiba TC58NVG1S3H NAND (2048+128-byte pages, BCH-32/1024). The board ran the
7.3.0-rc3 integration image, not the v10 submission. The module and sanitized
capture are published under `hardware/`; original flash backups and connection
details are not published.

Read-only measurements covered 14 transfer-count encodings, both documented
1 KiB SRAM banks, command-done W1C behavior, 216 randomized reads and 48
read-tail/accessor combinations. Destructive tests used only the authorized
backup SPL block: eight native-encoder images, twelve independently encoded
error-injection images, and sixteen normal-command randomized write/read
cases. Explicit PIO ECC reads complemented native callbacks. Original raw
data and OOB were restored after both destructive runs; all four SPL copies
were compared with their pre-test backups. The module was unloaded and a
subsequent 32 MiB MTD read completed without new controller/ECC log errors.

These measurements disprove the earlier assumption of a continuous H616
normal-command randomizer stream. Writes restart at each command; reads
restart in block mode but continue in page mode. A non-word-sized read leaves
its final requested byte without randomizer XOR. Historical continuous-stream
fixtures remain conditional tests, not silicon evidence. The tested native
BCH page format matched independent encoding across every physical byte.
No production driver change is inferred solely from the synthetic stream
witnesses; other admitted formats still need a caller audit.

The full suite passes **63/63 jobs** in `results/suite-odajhe78/` against the
unchanged v10 driver SHA-256
`f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6`.
Executable inputs stayed unchanged during that run. The catalog passes
**898/898 expectations across 43 models**; dedicated jobs contribute 278,
giving **1,176/1,176 matched TLC outcomes**. This includes the new 23-case
`NandMeasuredStream` matrix and its expected incompatible-format witnesses.
Offline capture replay passes 170 checks. The low-level source runner passes
44 added v10 read/write cases using the measured H616 behavior, under its
sanitizer and coverage builds.

Reproduction and remaining hardware limits are recorded in
[HARDWARE-MEASUREMENTS-20261104.md](docs/HARDWARE-MEASUREMENTS-20261104.md).
In particular, this does not prove hung-DMA retirement, all IRQ/reset ordering,
other ECC strengths or geometries, older controllers, or power-loss behavior.
Native callback invocation does not prove DMA was used on every transfer.
No kernel submission was changed by this measurement work.
