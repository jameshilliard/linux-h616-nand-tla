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
The independent [mtdchar chunking validation](docs/MTDCHAR-CHUNKING.md) models
MEMREAD/MEMWRITE allocation, page alignment, separate data/OOB cursors, partial
failures and ECC-result propagation. It checks actual ioctl call traces and
retains a separate, reproducible pre-existing uninitialized-statistics finding.
Deeper checks execute scan/cleanup and ONFI negotiation, compare pre-series and
BSP layouts, compose real BCH correction with the driver, and inject compound
read/BBT failures.
Boundary checks connect real core requests to persistent sunxi page operations,
decode ONFI/ID values into attach decisions, execute MTD reference/partition
removal and provider IRQ/tasklet paths, and detect historical bug patterns.
The [OOB-end v2 checks](docs/BOUNDARY-VALIDATION.md#oob-end-v2-trailing-partial-pages)
also exercise read-only partitions ending inside a page, with independent
baseline, v1 and corrected-candidate expectations.
The [late manufacturer-init checks](docs/BOUNDARY-VALIDATION.md#late-manufacturer-initialization-and-marker-compatibility)
exercise a scrambling flag set after controller attach. They reproduce the
ordinary-format marker regression in the pinned submission and check the v11
correction against existing-image reads and new-write representation separately.
The [current verification index](docs/CURRENT-DRIVER.md) describes the
30-patch v11 draft, the independently applicable existing-driver prefix,
and the [prerequisite review corrections](docs/PREREQUISITE-REVIEW-V2.md).
The [alias-free BootROM layout checks](docs/H616-RETAINED-LIFETIME-HARDWARE.md#alias-free-bootrom-layout-host-validation)
exercise U-Boot's OS partition fixup with libfdt and sanitizers, including
legacy layouts and rejection of partial overlaps. These are extracted-code
host checks, not an additional TLA+ proof or a new hardware result.
The [boot-format partition checks](docs/BOOT-FORMAT-PARTITIONS.md) cover the
`allwinner,boot-format` boolean in one fixed partition map. Extracted-code
checks exercise routing, geometry, overlap rejection and registration failures.
The [earlier compatible-based prototype](docs/BOOT0-PARTITION-MAP.md) records
the old selector and bootloader handoff experiment, not the current binding.
The earlier [minimal v11 draft](docs/MINIMAL-V11.md) keeps v10's organization and
checks the live scrambling flag in the ordinary marker callback. It uses
one ordinary-format operations table and the original NAND-core BBT scan.
The experimental [single-install policy checks](docs/BOUNDARY-VALIDATION.md#single-install-oob-policy)
and [generation-specific geometry checks](docs/GENERATION-OOB-POLICY.md)
remain as historical validation of the larger refactor, not descriptions
of the current submission. First-patch and final-tree checks are recorded
separately.
The [complete timing calculation checks](docs/TIMING-PROOFS.md) add an
exhaustive finite-domain proof of the 19-constraint minimum-period fold and
connect the register/clock/EDO calculation to 1,186 actual C observations.
Both controller timing-table generations, check-only behavior and deliberate
arithmetic/clock-contract failures are covered, with explicit bounds.
The [bit-accurate randomizer checks](docs/RANDOMIZER-BIT-CONFORMANCE.md) connect
format behavior to actual source-helper register/marker observations, an exact
15-bit LFSR model, and all 128 entries of both ECC seed tables. Independent
inputs distinguish compatibility from a matching write/read error.
The [stateful hardware/randomizer model](docs/RANDOMIZER-HARDWARE-STATE.md)
then separates programmed seeds from active state across commands, aborts,
resets and chip changes. It replays 3,184 stored H616 comparisons and audits
four older Allwinner BSPs. Older hardware uncertainty is checked separately
from the BSP-compatible codeword contract, not filled in with H616 behavior.
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
against the pre-series NAND base, and are now carried as patches 18 and 19
of the earlier 24-patch sunxi v10 draft. The source fixtures also test partial
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
The independent lifetime series now has pinned candidate-source checks and a
disconnect/reference model, including suspended waiters and nested partition
ownership. Real-kernel tests exercise unbind/rebind with synthetic controllers
under KASAN and lockdep. The [H616 retained-lifetime hardware run](docs/H616-RETAINED-LIFETIME-HARDWARE.md)
adds real-controller removal/reprobe with retained native/Boot0 file descriptors
and NVMEM consumers, followed by bounded read/write checks and raw restoration.
Exhaustive registration fault injection and other physical controllers remain
outside that run; these checks do not prove all MTD backend lifetimes.

The historical [device release-owner checks](docs/DEVICE-RELEASE-OWNER.md) distinguish
callback return from its final module reference drop. They reproduce the
self-put race, check the opt-in device-core ordering, and exercise concurrent
unload with an independently held device and a blocked release callback.
The [disconnected-lifetime experiment](#disconnected-lifetime-experiment)
instead separates class-device allocation from provider metadata and drains
NVMEM callbacks before provider teardown, following the v2 review. Its checks
are separate from the preserved sent-series models.

The [2026-11-04 real H616 measurements](docs/HARDWARE-MEASUREMENTS-20261104.md)
add a restricted diagnostic module, sanitized captures and offline replay.
They confirm count/SRAM/BCH contracts but disprove the earlier continuous
normal-command randomizer assumption. The measured restart, read/write asymmetry and
non-word-tail behavior now has its own TLA+ and actual-source checks. Older
controller behavior and genuinely failed DMA/reset retirement remain unproved.

The [extended v10 hardware tests](docs/HARDWARE-EXTENDED-20261104.md) exercise
explicit PIO/MDMA paths, all 64 seeds used by this chip, all 15 documented BCH
strengths as isolated codewords, MTD callers, stale metadata, memory load and
interrupt-timeout rechecks. All four SPL backups were restored and verified.
The captures also retain an **unresolved first-read-after-active-reset anomaly**;
quiet DMA memory does not establish that the next read is correct. Two further
models check randomizer enable transitions and those recovery evidence limits.

The [post-abort investigation](docs/POST-ABORT-DATAPATH.md) adds 396 controlled
trials with distinct synthetic pages. Raw and ECC reads expose incorrect
first-block placement after soft reset; clearing SRAM or resetting the NAND
chip does not reliably recover it. AHB bus reset passed the bounded comparison.
A new request-level model separates internal data ordering from reset
completion, DMA retirement and successful ECC status, without assuming a
universally safe recovery recipe.

The [recovery-composition extension](docs/RECOVERY-COMPOSITION.md) adds full
recovery-step failures, SRAM placement, staged/confirmed/torn writes, repeated
faults and measured randomizer behavior through actual page/OOB requests.
It identifies a large vendor-tail software/hardware stream mismatch on an
admitted synthetic geometry; the tested 128-byte OOB layout remains passing.
Current-source behavior and candidate bus-reset/quarantine policies stay
separate, and successful model runs do not establish unknown reset guarantees.

The [reviewable v10 update](docs/REVIEWABLE-V10.md) folds the vendor-tail
correction into the format patch, separates preparation, fixes and
optimizations, and requires positive candidate decoding checks. Larger-page
fixtures validate programmed user-data registers, each DMA batch and failures
in every later batch. The recovery model now varies stop and bus-reset
retirement guarantees independently; the physical post-abort anomaly remains
unresolved.

The historical [H616 private-buffer implementation](docs/H616-ABORT-RECOVERY.md)
added two patches before the optimizations and format work, producing a
26-patch draft. Controller-owned DMA storage and an
H616-specific AHB reset address the observed first-read corruption; failed
recovery blocks further I/O and retains unsafe DMA allocations. The actual
driver passed 192 injected read-abort/retry trials and 2,000 subsequent stress
reads, with every SPL backup restored. A new 41-case policy model, exact-helper
error checks and sanitized capture replay retain explicit limits on failed
silicon reset, older controllers and performance. Earlier reports remain
historical records, including their original unresolved findings.

The subsequent [streaming-DMA investigation](docs/H616-DIRECT-DMA.md) tests
whether the full-size private coherent data buffer is necessary. Read-only
H616 measurements cover immediate buffer reuse, observable late-write controls,
actual ECC abort/retry, and a corrected-ECC outlier with follow-up stress.
That investigation left the kernel draft unchanged. Nine added model
configurations distinguish safe direct DMA after retirement from
missing/failed-reset lifetime failures;
the reset's physical retirement contract remains explicit.

The [streaming-buffer implementation](docs/H616-STREAMING-IMPLEMENTATION.md)
then folded recovery into one patch, producing a **25-patch v10 draft**. It
requires an exclusive AHB reset, retains caller mappings until completion or
reset retirement, and releases mappings without MMIO after failed deassertion.
The full-size private coherent data allocation and per-transfer copies are
gone; the small coherent descriptor remains. That snapshot retained the
request indefinitely on assertion failure; its report and hardware capture
retain their original source identity.

The [current reset-error policy](docs/H616-RESET-ERROR-POLICY.md) propagates an
unexpected reset error and blocks further commands without an indefinite retry.
It relies on the supported H616 CCU callback/lifetime contract. The 26-case
model and actual-source tests explicitly retain failed assertion with active
DMA as an unsafe-unmap counterexample: error propagation does not itself
retire DMA. That policy report retains its original validation record.

The [fresh current-source hardware checks](docs/H616-REQUEST-FAULTS-20261105.md)
add 32 pre-confirm DMA write aborts and 256 PIO timeout/reuse trials, including
105 actual timeout returns and 97 observed late command-complete events.
Every first follow-up read matched; three ordinary-kernel 64 MiB reads then
completed with nine corrected bitflips and no failed ECC. All flash backups
were restored and normal boot returned. The report preserves raw-backup
discrepancies and distinguishes the tested finite delays from stuck PIO,
failed-reset and older-controller assumptions.

The [provider-owned pulse update](docs/H616-RESET-PULSE.md) then switches
H616 probe and recovery to `reset_control_reset()`. Its source fixtures check
the CCU's 10 microsecond pulse, reset-core errors and repeated exclusive
resets. Source/model checks and cross-builds pass. A subsequent
[read-only hardware check](docs/H616-RESET-PULSE-HARDWARE.md) passes 128
injected abort/retries and 152 control/follow-up reads with this API/timing
revision. All raw snapshots remain unchanged and normal boot is restored.
The earlier captures keep their original driver digests and are not
presented as measurements of the new pulse.

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
python3 run_randomizer_hardware.py --download
python3 tests/test_randomizer_controller.py
python3 run_c.py
python3 run_lowlevel.py
python3 run_timing.py --download
python3 run_core.py
python3 run_lifecycle.py
python3 run_providers.py
python3 run_codec.py
python3 run_oob_pipeline.py --download
python3 run_composed.py
python3 run_transport.py
python3 run_transport.py --count-boundary
python3 run_discovery_boundary.py
python3 run_bbm_init.py
python3 run_lifetime_boundary.py
python3 run_provider_boundary.py
python3 run_historical.py
python3 run_mtd_boundary.py
python3 run_ordering.py
python3 run_request_refinement.py
python3 run_fd_locking.py
python3 run_fd_lifetime.py
python3 run_retained_lifetime.py
python3 run_retained_model.py
python3 run_concat_lifetime.py
python3 run_hardware.py
python3 run_measured_requests.py
python3 run_mtdchar_model.py --download
python3 run_mtdchar.py
python3 run_mtdchar_admission.py
python3 run_mtdchar_series.py
```

The retained-handle runners cover the separate raw-NAND lifetime series. Run
`run_retained_model.py --download` for its pinned TLC 1.7.4 models, and
`run_retained_lifetime.py` for the committed C-function snapshot under
ASan/UBSan. The latter also accepts `--kernel-tree` to check another candidate.
Its module-state cases distinguish live bindings from failed initialization
and module unload: the latter two must finish deferred releases before module
text can disappear.

`NandControllerLifetime` checks two successive bindings with up to two chips
sharing each controller. It covers retained MTD and device references,
operation draining, independent controller/chip releases, and replacement-card
isolation. Negative controls free either object too early, omit the drain, or
reuse card storage. These are bounded ownership models, not simulations of
every controller's interrupts, DMA aborts or hardware registers.

`MtdOtpLifetime` checks user/factory OTP ownership independently of MTD node
registration, including failed registration and bare NVMEM device references.
Its negative controls omit the context reference, release it at unregister,
or unregister the same provider twice. The [OTP lifetime report](docs/OTP-LIFETIME.md)
separates these bounded checks from the source-extracted and real-kernel tests.

`audit_retained_drivers.py --kernel-tree PATH` inventories all 52 raw-NAND
scan-provider families and their ownership hooks, including the SmartMedia
and BCM47xx scan wrappers. This structural check detects missing conversions;
it does not prove each driver's error-path ordering. SPI NAND, OneNAND and
other MTD technologies are outside this inventory.

The modules under `tests/kernel/retained_lifetime/` exercise the real MTD core
and raw-NAND helpers. The synthetic controller test scans two chips, retains
handles and a bare device reference across unbind, checks that old reads fail,
and probes a replacement controller before dropping those references. Its
release counters check that both generations are reclaimed exactly once. Run
the supplied initramfs `init` with a kernel built with raw NAND, MTD NVMEM,
KASAN and lockdep; the tests do not access physical flash. The `run` module
parameters execute after module initialization, while the module is live.
The OTP module also tests retained user/factory consumers, provider module
references, a bare NVMEM device reference, a consumer acquired before failed
probe, and OTP/NVMEM registration errors. Successful runs must unload each
test module before reporting success.

`run_concat_lifetime.py` checks two virtual-concat component slots and a
replacement binding. It models staging, completed registration, immutable
published component sets, file and NVMEM references, failed probes and final
backing release. Negative controls remove the backing or component pin,
publish before registration finishes, or leave a failed probe staged. These
checks assume opt-in backing ownership and serialized staging; they do not
model legacy unmanaged MTD allocation or hardware I/O.

Build the optional `virt_concat_lifetime` test module with
`CONFIG_RETAINED_CONCAT_TEST=m` and a kernel with `CONFIG_MTD_VIRT_CONCAT=y`
(which requires `CONFIG_MTD_PARTITIONED_MASTER=y`). Its private DT fixture
requires OF dynamic/overlay support and an empty DT, as on the QEMU x86 test
machine. It checks reverse probe order, appended offsets, retained MTD/NVMEM
handles, rebind isolation, failed registration and concurrent removal of
both masters. No physical flash is accessed. The injected DT is intentionally
kept until the disposable guest shuts down.

The three mtdchar model/trace/admission runners independently pin stable
TLA+ Tools v1.7.4; their first command downloads and verifies it. This avoids
the rolling prerelease download used by the historical suite. Its older pin
remains unchanged, and its URL may now fail checksum validation on a fresh
download. The C-only series runner additionally checks each intermediate patch.

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
| `NandTimingFold` | Full minimum-period fold over every 19-element vector in the configured finite domains; zero-hold-time read-access constraint |
| `NandTimingProgram` | C-connected full timing calculation, minimal LUT encodings, field packing, ns rounding, EDO, check-only and clock failures |
| `NandRequest` | Core data/OOB copy ordering, read retry, ECC statistics, return lengths and transport-error prefixes |
| `NandBbt` | Factory-marker scan, RAM/flash tables, runtime markbad, optional OOB markers, mirrored updates and remount |
| `NandFormat` | Page-local PIO/DMA batching, seed selection, stored segment identity and reader compatibility |
| `NandOwnership` | CPU/device visibility, descriptor publication, streaming-map ownership and backend register restrictions |
| `NandPersistence` | Interrupted mirrored BBT erase/program, eight-bit version rollover and detectable/undetected torn tables |
| `NandRt` | Fine-grained threaded-IRQ draining and safety versus eventual scheduling/liveness |
| `NandProgram` | Finite partial-program budgets, bit monotonicity, erase, torn programs and no implicit write replay |
| `NandRecovery` | Backend stop, outstanding bus transactions, IRQ drain and late controller completion are separate obligations |
| `NandPostAbortRequest` | Post-abort internal data ordering, separate CMD/DMA completion, ECC status versus delivered bytes, continuation policies and conditional recovery across two requests |
| `NandPrivateRecovery` | Historical H616 private-buffer policy, IRQ/reset ordering, failed-recovery closure, context restoration and no write replay; explicit weaker-reset counterexamples |
| `NandStreamingRecovery` | Current caller-buffer policy: mandatory reset, error propagation, held-reset cleanup, no write replay and explicit failed-assertion/active-DMA counterexamples |
| `NandRecoverySequence` | Complete recovery-step ordering, failed reset/context restore, PIO error reuse, persistent device events and candidate quarantine |
| `NandSramPlacement` | Rotated/oversized post-abort SRAM placement, failed-prefix copying and fresh subsequent request bytes |
| `NandProgramRecovery` | Staging versus confirm/array effects, ambiguous program completion, partial media, failed NAND reset and no rollback assumptions |
| `NandMeasuredPage` | Measured NORMAL streams through whole protected/tail requests, software-reader mismatch, partial errors and request reuse |
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
| `NandRandomizerStream` | Conditional continuous-stream representations, seed changes, zero-length requests and partial errors; not the measured H616 contract |
| `NandMeasuredStream` | Observed H616 normal-command restart, page-mode read/write asymmetry, non-word read tails and format-compatibility witnesses |
| `NandMeasuredToggle` | Observed keystream phase across randomizer disable/re-enable, separated for read/write and block/page modes |
| `NandRandomizerBits` | Exact numeric recurrence versus a fifteen-bit shift register; all 16-bit input seeds for one step, bounded multi-step and byte-order checks |
| `NandRandomizerPhase` | All BSP page seeds advanced through 512-/1024-byte ECC steps plus fifteen bits; source table entries and exact software bytes |
| `NandRandomizerPolicy` | Source-observed finalized policy, seed/register preservation, independent marker bytes and failed-transfer cleanup; dedicated runner |
| `NandRandomizerController` | Exact active LFSR versus programmed seed, mixed commands, abort/reset/chip-switch composition, measured H616 bytes and separate older-BSP/uncertainty profiles; dedicated runner |
| `NandResetObservation` | Retained-buffer lifetime and the distinction between reset/quiet observations and correct first subsequent data |
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

The standard suite defines 1525 TLC configurations across 55 models, including positive checks,
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

## Disconnected-lifetime experiment

This checks the v3 redesign following
[Greg's v2 lifetime review](https://lore.kernel.org/r/2026111037-stir-hermit-cb21@gregkh/),
prepared for submission. It separates retained data from provider
code instead of extending `struct device` with a module owner. The historical
release-owner snapshots above remain unchanged.

The kernel changes cover NVMEM callback retirement, separately allocated MTD
class devices, and virtual-concat disconnection. They are carried by
`submit/rawnand-retained-handle-lifetime-v3`, based on NAND-next
`30daaf3e7897` and its posted prerequisites. The reviewed candidate tip is
`a7881de8792602f297b39905eba1e90d78217acc`. Source-correspondence runners
record the exact kernel commit and checked function hashes. The abstract
models can run independently of the kernel checkout.

| Object | Lifetime in the redesign |
| --- | --- |
| NVMEM provider callbacks/context | Unregister closes admission and waits for SRCU readers, including cell post-processing and DT fixups. |
| Retained NVMEM consumers | Keep core metadata; subsequent I/O returns `-ENODEV`. |
| MTD backing data and nested partitions | Explicit metadata references remain independent of bare device references. Existing MTD handle/module ownership is retained. |
| MTD class and class devices | A small built-in MTD component owns class data, the device type, PM dispatch and final device release, even with `CONFIG_MTD=m`. |
| MTD class PM callbacks | Detachment takes the device lock to drain active callbacks and clear the backing association. Later callbacks are harmless without retaining an MTD handle. |

`NvmemDisconnect.tla` separates reader entry, admission, callback return,
post-processing, SRCU exit, disconnect, drain and provider unload. Two positive
cases and four mutations check that no provider callback or context is used
after retirement. SRCU's grace-period contract is assumed, not proved in LKMM.

`MtdDetachedDevice.tla` models nested metadata ownership, a retained hidden
concatenation component, open handles, delayed device/class release and module
unload. Four positive cases cover two depths and normal/failed-initialization
teardown. Five mutations remove a required parent reference, embed retained
device storage in freed metadata, retain a late module callback, allow an API
caller to disappear during its epilogue, or place class data in module storage.
API caller residency is an explicit contract; the model does not establish
that every kernel caller satisfies it.

`MtdPMDetach.tla` separates the initial lock inside `device_del()`, removal
from the PM list, a queued callback's device-lock acquisition, callback return,
backing detachment and metadata release. Two positive cases and two mutations
check that detachment both drains admitted callbacks and stops later backing
access. It assumes the device mutex contract rather than proving the full PM
scheduler. MTD's PM dispatch and device-type storage remain built in so retained
devices do not expose module-owned tables.

`CallbackRetirement.tla` adds two generations of callback-backed metadata,
representing layout cells and virtual concatenations. Two positive cases and
four mutations check retained handles, callback draining, disconnection checks
and replacement identity. This is a safety model: it makes no fairness or
termination claim. Its source checks cover every concat forwarding operation,
including panic writes and OOB-layout callbacks, and cell callback retirement.

```sh
python3 run_nvmem_disconnect.py --download
python3 run_mtd_detached_device.py --download
python3 run_mtd_pm_detach.py --download
python3 run_callback_retirement.py --download
python3 run_nvmem_disconnect.py --kernel-tree ../linux
python3 run_mtd_detached_device.py --kernel-tree ../linux
python3 run_mtd_pm_detach.py --kernel-tree ../linux
python3 run_callback_retirement.py --kernel-tree ../linux
```

The kernel fixtures use disposable QEMU devices, not real flash. NVMEM tests
block read, write and post-processing callbacks while removing the provider,
then exercise stale consumers and reuse the provider name before final device
release. Dynamic layout tests hold post-processing active while unbinding,
rebind the layout, and check that the new cell works while the retained old cell
stays disconnected. MTD tests retain nested devices and an open file across
disconnection, unload the provider and MTD core, reuse the device numbers, and
exercise module initialization rollback. The PM fixture admits a class callback
after `device_del()` releases its initial lock, holds it while removal progresses,
and checks that backing retirement waits. It reproduced premature retirement
before the fix. Retained-device tests also invoke the built-in PM callbacks
after the provider and MTD core modules have unloaded. A synthetic raw NAND
controller additionally checks unbind/rebind with old handles and independent
device references. Virtual-concat tests cover concurrent removal of managed and
unmanaged component masters,
rebind and partially published registrations, with 25 iterations per mode.

```sh
python3 run_detached_lifetime_kernel.py --suite nvmem \
  --kernel-tree ../linux --kernel-build ../build-x86 \
  --busybox ../busybox-static
python3 run_detached_lifetime_kernel.py --suite mtd \
  --kernel-tree ../linux --kernel-build ../build-x86 \
  --busybox ../busybox-static
```

The kernel build must be configured for x86-64 QEMU with MTD and raw NAND as
modules, virtual concatenation enabled, NVMEM and its layout support built-in,
OF and flattened-DT support, system sleep (`CONFIG_PM_SLEEP=y`), an initramfs
console, sysfs, procfs and devtmpfs. The runner rejects an MTD configuration
without system sleep; `--mtd-pm-only` isolates the PM removal interleaving.
The fixture builds also require `dtc` from the kernel build and host `xxd`.
The recorded runs enable `CONFIG_MTD_PARTITIONED_MASTER`, KASAN, lockdep and
`CONFIG_DEBUG_KOBJECT_RELEASE`. The latter also requires the debug-object timer
options; verify the resulting configuration rather than just requesting it.
Each runner saves build/QEMU logs and its result under `results/`. Pass
`--accel kvm` when available; the default is software emulation. Private fixture
DT nodes are deliberately retained until the disposable VM exits. They can
produce missing `of_node` sysfs-link messages because they are not installed
in the global firmware tree; they do not represent a hardware DT test.

All 25 model expectations matched. The expanded MTD and NVMEM kernel cases
passed with the diagnostic configuration, and x86 allmodconfig builds of
`drivers/mtd/`, `drivers/nvmem/`, the PTP OCP and cobalt MTD users passed with
`W=1`. i386, MIPS and PowerPC MTD/NVMEM builds also passed, including the
PowerPC-only raw-NAND drivers. Each of the six core patches built individually
against x86 allmodconfig. Delayed-release testing
first reproduced a crash in `class_release()`: moving only the device release
callback was insufficient because the class object was still module data.
Moving that class data into the built-in component fixed the tested failure;
the class-storage mutation preserves the counterexample.

The review used the general kernel, locking, allocation, driver-core/kobject/
sysfs/debugfs, PM and false-positive lifetime guidance from Sashiko's prompts at
`ffa8b3489407dd5cea6558fcffb6a4667131f1d3`.
The structural inventory checks 52 raw-NAND driver families, but it is not a
proof of their hardware teardown. Exhaustive post-publication allocation-fault
injection, weak-memory analysis and runtime validation of every controller
remain outside this coverage. PTP OCP's bare-device lookup now acquires an MTD
handle; the model does not automatically discover equivalent misuse by other
clients. NVMEM's legacy configuration owner field remains for source
compatibility. No real hardware test is claimed for this redesign.

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

## CI

[.github/workflows/mtdchar.yml](.github/workflows/mtdchar.yml) runs the mtdchar
protocol, extracted-source and admission-statistics checks on pushes and pull
requests. A local pass does not imply that a remote Actions run has completed.

[ci/validate.yml](ci/validate.yml) is a pinned GitHub Actions workflow template.
It runs host source/model suites and sanitized hardware-capture replay on
Ubuntu 24.04 with Java 21 and retains diagnostics and
counterexample traces. This broader workflow remains a template outside
`.github/workflows/`. Install it as `.github/workflows/validate.yml` using a
workflow-authorized credential to activate it.

Repository-authored code is GPL-2.0-or-later. See `LICENSE`; the reference binding
retains its dual-license SPDX notice, and the NAND-core excerpts retain their
GPL-2.0-only notices.
