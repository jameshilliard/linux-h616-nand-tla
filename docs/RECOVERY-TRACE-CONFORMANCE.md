# DMA recovery helper traces checked by TLA+

`NandRecoveryTrace` connects the unchanged DMA preparation, interrupt/wait,
abort, cleanup and chip-selection helpers to an independent protocol checker.
The older recovery models explore abstract interleavings; this extension checks
the concrete boundary sequence produced by those helpers under a specified
fixture schedule. It does not translate arbitrary kernel executions to TLA+.

## Source and hardware boundaries

`run_lowlevel.py --recovery-traces` extracts the real function bodies and wraps
their DMA API, MMIO, completion and interrupt services with observers. No kernel
body is edited. Observations include mapping counts, engine activity, outstanding
bus accesses, status and interrupt masks, completion state, IRQ disable state,
the RAM access-mode bit and the selected chip's register context. The checker
computes transitions independently and compares its state with those snapshots.

The caller script invokes the helpers, supplies command-launch boundaries and
reuses the controller. It **does not execute the entire ECC page caller**.
Separate page/transport checks cover that control flow. In particular, using
the low-level helpers in both DMA directions does not prove that every generated
schedule is reachable through every page, subpage or OOB operation.

Device event delivery, bus retirement and the effects of reset remain explicit
fixture contracts. For H616 the launch fixture clears CMD/DMA status at FSM
start, following the existing manual-backed register model; it does not assign
that undocumented behavior to the A10/A23 profiles. Clearing status at launch
cannot prevent an old event from arriving *after* launch.

The context check uses intentionally recognizable test values for chip select,
timing, spare-area offset and the cached clock. They are fixture inputs, not
recommended hardware timings. Same-clock reuse must reprogram the context even
if reset cleared its registers; a cached clock is not proof of valid registers.

## Finite input matrix

Each source version executes 264 schedules: three DMA backends, two directions,
retained/cleared reset context and same/different chip select. The second chip
select uses the same clock, exercising the cached-clock path.

| Normal schedule | Expected behavior |
| --- | --- |
| Events already present when the wait arms its interrupt | IRQ acknowledges the required events and completes the wait |
| CMD then DMA | Integrated DMA waits for both; external DMA waits only for CMD |
| DMA then CMD | Arrival order does not change the integrated-DMA completion requirement |
| Status arrives at the timeout recheck without an IRQ handler | The status recheck accepts completed work |
| Only CMD acknowledged, then timeout | Integrated DMA still times out waiting for DMA; external profile receives no event in this case |
| Remaining event/handler runs while `disable_irq()` drains it | Empty pending mask is accepted without a false timeout |
| No event | Timeout, backend-specific abort, cleanup and subsequent reuse |

The 168 normal-contract schedules each execute a second successful transfer.
The other 96 schedules deliberately weaken one contract:

| Contract variant | Required witness, not a newly established silicon defect |
| --- | --- |
| Abort cannot stop the transfer | Unmap while the engine remains active |
| Stop returns successfully but leaves a bus access outstanding | Unmap before memory ownership is safe to release |
| An old command's events survive recovery and arrive after the next launch | Stale completion of the new wait, followed by unmapping its still-active transfer |
| Completion flags become visible before the last bus access retires | Successful wait followed by unsafe unmap |

Outstanding bus accesses include reads from a TX buffer as well as writes to
an RX buffer. These cases do not simulate actual DMA payloads, CPU caches,
IOMMU behavior or bus transactions. `dmaengine_terminate_all()` is a provider
boundary, not a model of the provider's physical implementation.
The pinned sun4i/sun6i termination callbacks return success; the injected
negative return is therefore a conditional API-boundary witness, not evidence
that these providers currently return that error. A successful software reset
or completion flag also needs the corresponding physical retirement guarantee.
See [the hardware audit](HARDWARE-CONTRACTS.md) and
[the existing refinement limits](REFINEMENT-VALIDATION.md).

## Independent checks and negative controls

The model checks helper ordering, balanced map/unmap and direction, the required
abort backend, post-wait interrupt masking/draining, post-cleanup RAM mode,
same-clock context restoration and final ownership accounting. Success requires
events from the current transfer, tracked separately from raw status bits.
Engine activity and pending bus work are separate state variables.

All traces, including the weakened-contract inputs, must pass structural and
snapshot checks. At each trace's end, `ContractOutcome` checks the expected
safety result **from the selected input contract**, not from the fixture's
reported result. Thus a lost normal-path guarantee or a missing conditional
witness fails the run. Twelve dedicated negative configurations additionally
require `MappingSafe` or `FreshCompletion` to fail: one per backend and weakened
contract. Eight altered-trace controls check snapshot corruption, wrong unmap
direction, missing unmap, wrong context, incorrect timeout return, early unmap,
late IRQ-mask restoration and omitted abort.

Input validation requires every one of the 264 profile combinations exactly
once, verifies the trace digest, and requires every observation kind. Identical
boundary projections can be checked once with their multiplicities retained.
Wrappers contain at most two projections to bound TLC's constant footprint.
The sanitized and separate GCOV executions must produce identical trace bytes.
Reports retain driver, extracted-function, fixture, model and trace digests.

The dedicated runner is separate from the ordinary handwritten configuration
catalog. Its counts are not silently added to the standard suite total. The
CI template includes a separate recovery-trace job; its presence does not mean
the template has been installed or executed by GitHub Actions.

## Reproduce

Capture the pinned reference and substitute the printed result directory:

```sh
python3 run_lowlevel.py --recovery-traces
python3 run_recovery_refinement.py results/lowlevel-REFERENCE --download
```

For the current fixed candidate:

```sh
python3 run_lowlevel.py --recovery-traces \
  --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c \
  --expect-count-fixed
python3 run_recovery_refinement.py results/lowlevel-CANDIDATE
```

`--case` can select one wrapper or control for debugging. Only an unfiltered
run checks all projections and controls. A parser error, sanitizer failure,
timeout, JVM failure or unrelated invariant violation is never accepted as a
passing counterexample. The ordinary low-level runner remains usable without
trace collection.

## Remaining limits

This enumerates fixture schedules, not all possible kernel interleavings.
DMA preparation failures, arbitrary sequences of repeated errors, different
clock rates, polling mode and partial-page data preservation are covered by
other checks, not this trace matrix. The launch boundary is supplied by the
script; it does not prove the driver's complete caller-to-provider path.

Physical DMA retirement, reset/event causality, posted-MMIO ordering and actual
SMP/RT execution remain unresolved hardware/kernel-integration boundaries.
The traces expose the consequences of weakening those guarantees; they do not
establish that the weak variants describe real hardware. The retained-MTD/unbind
problem and randomizer phase/reload questions also remain separate. No hardware
access, flash write or kernel implementation change is needed for these checks.
