# Extended driver coverage

All seven follow-up areas have executable models and corresponding source-level
checks or an explicit boundary. These are bounded models of contracts, not a
claim that every execution of every driver function has been proved correct.

## Model-to-source map

| Area | Model and bounded scope | Executed source / independent checks |
| --- | --- | --- |
| Combined controller recovery | `NandController`: two operations, two batches each, DMA preparation/completion, IRQ/poll timeout/drain/recheck, failed DMA read followed by PIO, repeated failures, no partial-write replay, one confirm; `NandIrq` retains finer register-read/write interleavings | Actual `sunxi_nfc_interrupt()`, `sunxi_nfc_wait_events()`, reset/abort/cleanup; page wrappers run separately with transport fault injection |
| DMA backends | `NandDma`: external DMAengine, A23 register MDMA, H616 descriptor MDMA; map/prep/submit failure, alignment/16-bit length, 32-step mask, descriptor stores/barrier/fetch | Actual `dma_op_prepare/cleanup/abort` and `dma_init`; literal expected little-endian descriptor words, mapping balances, DMA direction, RAM access mode, PIO fallback and probe defer |
| Command/SRAM handling | `NandCommand`: 1 KiB parser limits versus 1/8 KiB SRAM, split offsets, write staging before issue, reads after completion/readiness, error exit and pure check-only | Actual `exec_op`, `exec_subop`, software readiness wrapper, raw buffers, unchanged NAND-core parser/patterns/subop helpers; 3,001-byte transfers through real parser splitting, malformed instructions and address split witness |
| Payload/media identity | `NandMedia`: two pages, six abstract byte cells representing BBM/data/protected OOB/parity/tail; distinct values, old-AND-new programming, arbitrary partial subset on failure, untouched ranges | Raw SRAM/read/write/randomizer functions with nonuniform multi-page bytes, repeated program bit monotonicity and failure prefixes; actual protected-byte getters/setters and BBM transformation with parity/tail sentinels |
| Core/format integration | `NandCore`: generic/vendor admission, small-page software fallback, whole-page vendor classification, transport-vs-ECC return semantics, one retry with stat rollback, bounded wrap | Actual attach/init/vendor subpage wrapper and 32-bit ECC counter wrap. The rest of NAND-core read/retry/BBT behavior is an abstract contract, not compiled core code |
| Shared controller/lifecycle | `NandLifecycle`: two serialized chips, differing geometry/clock, transfer/IRQ lifetime versus removal; `NandProbe`: 19 probe/scan/register stages, two chip selects, manual and managed unwind | Actual alternating chip selection and DMA initialization/failure checks. Full device-core, devres, NAND scan, MTD unregister and platform probe/remove functions are not executed by the host harness |
| Timing | `NandTiming`: six unique A10/H616 lookup tables, ceiling and minimal encoding, rejection, check-only, faster-rounded-clock witness | Actual lookup and complete `setup_interface()` against an independently expressed constraint oracle; EDO threshold, failed/invalid clock rounding and alternating timing/clock context |

The three DMA profiles correspond to the submitted capabilities, not three
separately proven pieces of silicon. Buffer addresses are byte tuples in TLC
so that 32-bit high addresses can be represented without TLC integer overflow.
Actual C tests exercise the descriptor's byte encoding.

## Negative tests

Mutation tests must fail the named invariant, covering lost offsets, wrong page
or column, missing DMA barrier, count-versus-mask encoding, missing validation,
unsafe cleanup, dropped IRQ draining, program replay, stale chip context,
incorrect vendor classification, statistics rollback and rounding down.

Assumption tests deliberately remove successful DMA stop, external-command
completion implying DMA completion, PIO command quiescence after timeout,
suppression of old command events, core-user draining, successful clock changes,
or safe clock rounding. A counterexample identifies the dependency on that
contract; it does not prove a supported device violates it.

Source-contract tests reproduce actual implementation behavior for split
addresses and failed external DMA configuration. The clock-failure witness also
has a source-executed reproducer. See [FINDINGS.md](FINDINGS.md).

## Measured C coverage

Both C suites first run with ASan/UBSan. A separate `-O0 --coverage` build omits
sanitizer instrumentation, then maps GCOV data back to unchanged function bodies
using `#line`. This avoids counting sanitizer-generated branches or fixture
code as driver coverage. `coverage_report.py` unions the shared functions.

The recorded extension executes **82 of the 90 function definitions** in the
pinned driver. Within those extracted functions it hits **1,268 of 1,291 GCOV
lines** and **601 of 644 host-compiled branch edges**. These are not percentages
of all source or all kernel branches: fixture macros and structures differ
from the kernel, and compilation is host-side.

The eight unextracted functions are:

- `to_sunxi_nand()` and `to_sunxi_nfc()` (fixture conversions).
- `sunxi_nand_detach_chip()` and `sunxi_nand_chips_cleanup()`.
- `sunxi_nand_chip_init()` and `sunxi_nand_chips_init()`.
- `sunxi_nfc_probe()` and `sunxi_nfc_remove()`.

Their ownership/lifetime contracts are represented in the lifecycle/probe
models, but that is not a substitute for executing those C bodies. Missed lines
and branch edges remain visible in each generated report. In particular, some
timing rejection/constraint branches and nested OOB error branches are not hit
by these fixtures; no claim of 100% C branch coverage is made.

## Boundaries that remain

- Fine-grained IRQ register races are checked in `NandIrq`; the composed model
  combines the status/mask read into one action to control its state space.
- DMA descriptor visibility is an abstract CPU/device ordering model, not a
  Linux memory-model proof or IOMMU/cache-coherency emulator.
- The media model uses a reversible scrambling mask, not BCH. Raw C fixtures
  implement a separate bit-level LFSR but assume the modeled command/reset and
  media behavior. They do not prove the physical controller's seed reload rules.
- Model partial programs may alter arbitrary requested cells. C fault tests
  use a prefix. Device partial-program-count limits, erase, wear, retention and
  analog/power-failure behavior remain outside scope.
- The core model wraps bounded counters modulo 256; the C test checks actual
  32-bit counter wrapping. Neither proves arbitrary lifetime-wide statistics
  behavior or every MTD/read-retry algorithm.
- Probe models abstract successful cleanup by kernel services. No PM callback
  is invented: the pinned driver does not provide a suspend/resume protocol to
  verify. Device-tree parsing, regulator/pinctrl behavior and all optional-clock
  combinations are not exhaustively instantiated.
- There is no generated C-to-TLA+ refinement proof, complete kernel boot test,
  or new hardware test in this validation run.
