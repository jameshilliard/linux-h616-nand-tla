# H616 streaming-buffer recovery implementation

The v10 draft now has 25 patches. Patch 20, `e4d2008d73ac`, implements H616
recovery directly; the earlier private coherent data-buffer patch is removed.
Submission tip `f9d1d60fc9aa` and integration commit `557602a55834` contain
identical NAND driver bytes:

```text
183c5ff0ad786560110def4fd836bb42b40ff5eb884f1d633496c79e3d17f282
```

The 16-byte coherent hardware descriptor remains device-managed. Main data
uses the existing streaming SG mapping, without a private 32 KiB data
allocation or a per-batch memcpy. Existing mapping/alignment checks and
prepare-failure handling remain in place.

## Ownership and failure policy

| Boundary | Required behavior |
|---|---|
| Probe | Obtain a mandatory exclusive AHB reset for H616 and assert it before accepting requests; reject missing/unsupported reset |
| Normal completion | Wait for command and internal-DMA completion, then unmap in the original direction |
| Aborted transfer | Drain IRQ handling, soft-reset the controller, then assert its AHB reset before unmapping |
| Assertion failure | Log once for that abort episode; retain the request and mapping, retrying assertion once per second |
| Deassertion failure | Leave IRQ disabled, mark recovery failed, unmap after successful assertion, and avoid reset-held register access |
| Subsequent request | Reject it if recovery failed; otherwise reselect the chip and restore timing/page context |
| Interrupted read/write | Retry a read from a fresh whole-page PIO setup after successful recovery; do not replay an interrupted program operation |

The H616 CCU assertion callback performs a spinlocked register update and
returns zero. Failure injection tests the defensive contract, not an observed
CCU API failure. If assertion permanently fails, the request remains blocked;
it cannot safely return caller-owned memory. The model explicitly witnesses
that availability limit rather than claiming unconditional completion.

AHB assertion retiring outstanding bus traffic remains a hardware assumption
supported by the [read-only investigation](H616-DIRECT-DMA.md). A successful
reset API return is not itself a proof of physical retirement. Silicon with a
permanently wedged interconnect, interrupted flash programs, older controllers,
and generic retained-MTD/unbind lifetime are not newly validated by this change.

## Software checks

- `NandStreamingRecovery`: 28 expected TLC outcomes, including weakened
  retirement/data-path contracts, missing-reset admission, premature unmap,
  reset-held MMIO, write replay and permanent-failure liveness witnesses.
- The unchanged candidate DMA/reset helpers run under ASan/UBSan for 64
  direction/chip/soft-reset/assertion/deassertion combinations, repeated
  mapping reuse, and a bounded observation of permanent assertion failure.
- Actual read/subpage/write bodies exercise 11 held-reset failure paths,
  including write-column failures in later DMA batches, to
  check mapping cleanup and rejection of reset-held fallback or program
  continuation. The 1,273 lifecycle/ONFI cases include mandatory-reset
  acquisition and initial assertion failures (86,486 assertions).
- All 16 source/hardware-fixture suite jobs pass on the digest above with
  unchanged executable inputs during the run. This includes page/OOB/ECC,
  codec, low-level, probe, transport, measured requests and proximal interfaces;
  it does not imply that the entire driver is formally proved.
- Historical pinned-source tests remain valid and retain their known-bug
  witnesses; the old private-buffer model remains a historical policy model.
- ARM32 LLVM and ARM64 GCC `W=1` builds pass at each changed series prefix
  (patches 20 through 25). The ARM64 integration image builds successfully.
- The rewritten recovery patch passes checkpatch with no warnings/errors.
  Sashiko general/false-positive guidance and locking, MM allocation, MMIO,
  alignment, OF and build prompts were checked against actual callers and
  the H616 CCU implementation. No dedicated NAND prompt was available.

```sh
python3 run_suite.py --module NandStreamingRecovery
python3 run_lowlevel.py --candidate-source /path/to/sunxi_nand.c --expect-count-fixed
python3 run_lifecycle.py --candidate-source /path/to/sunxi_nand.c \
    --expect-available-children --expect-deferred-publication
python3 run_suite.py --candidate-source /path/to/sunxi_nand.c --source-only --workers 4
```

## Read-only hardware revalidation

The integration branch was synchronized into the normal build, then the
observation counters and one-shot read-timeout hook were installed only in
the disposable build tree. The resulting signed image was booted into RAM.
UBI was detached and the diagnostic module's write permission remained off.
No NAND erase/program requests were used.

Both diagnostic DMA modes now execute the production streaming path, without
temporarily replacing a driver buffer pointer. The PIO control still uses
the unchanged page callback. Fresh raw backups independently check the
existing PIO-decoded versus DMA-raw parity representation; parity bytes are
not silently excluded from comparisons.

| Check | Result |
|---|---|
| Timeout matrix | 128 abort/retry trials over two pages and eight injection delays; 111 captured an active-engine status |
| Control/follow-up reads | 152 normal PIO/DMA reads, including alternate-page reads immediately after recovery |
| Outlier-profile repetition | 400 additional reads, including 100 aborts at 5 us |
| Delivered data and OOB | All 680 reads matched their path-specific reference |
| ECC accounting | No corrected or failed ECC increments in these 680 observations |
| Delayed writes | Full allocation remained unchanged immediately and after approximately 1, 20 and 80 ms waits following return |
| Backup verification | All four SPL copies and the first UBI eraseblock unchanged |
| Ordinary MTD reads | Three 32 MiB reads after unloading the module passed: 2.05–2.08 s wall time, 0.29–0.31 s system time |

The read timings describe this diagnostic kernel, not a controlled
patch-attributed performance comparison. The immediate native guard check
retains the cache-visibility limitation documented in the earlier direct-DMA
investigation; the mapped late-write observation and that investigation's
raw guard controls provide separate evidence. No finite wait proves that
arbitrarily late bus writes are impossible.

The board was restored to its normal NAND boot. Diagnostic hooks were removed
from the host build tree with a source-digest check and the ordinary Image
rebuilt. Production source branches contain no test hooks.

The [sanitized capture](../hardware/h616-streaming-implementation-20261104.json)
contains only comparisons and register/counter observations, not flash bytes,
flash-content hashes, host paths or target identities. It has a standalone
replayer and 19 negative controls for evidence checking:

```sh
python3 hardware/streaming_implementation.py
python3 -m unittest hardware.test_streaming_implementation
python3 run_hardware.py
```

Earlier direct-buffer experiments remain separate supporting evidence; their
results are not attributed to this implementation snapshot.
