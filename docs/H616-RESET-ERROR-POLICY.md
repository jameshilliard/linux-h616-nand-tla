# H616 reset error propagation

This report records the policy change's initial validation. The later
[current-source board run](H616-REQUEST-FAULTS-20261105.md) uses the same driver
digest and adds write-abort, PIO-timeout and ordinary-kernel evidence without
changing the failed-assertion limitations below.

The current 25-patch v10 draft ends at `56d96fe94ff7`; recovery is patch 20,
`e53c8f2daddd`. The integration commit is `51601eff4029`. Both branches
contain identical NAND driver bytes:

```text
102b942b18d37e37f6f6dd605233da79149e2fbff61f38daf25acde75f0ce676
```

The recovery path attempts AHB assertion once. An unexpected assertion or
deassertion error is logged and returned, leaves the IRQ disabled, and marks
the controller unusable for subsequent commands. Cleanup avoids controller
register access after failed recovery. The one-second indefinite assertion
retry is removed. Successful recovery and normal DMA ownership are unchanged.
The abort helper returns the original reset error; any attempted read fallback
is rejected at chip selection with `-EIO` before another command is issued.

## Why this policy is H616-specific

`drivers/clk/sunxi-ng/ccu_reset.c` implements assertion and deassertion with
spinlocked reset-register updates and an unconditional zero return. The NAND
driver obtains a mandatory acquired exclusive reset and exercises assertion
at probe. The reset framework pins the provider module through the handle;
the H616 CCU driver suppresses sysfs bind/unbind attributes. The supported
provider path has no identified transient assertion error for a retry loop
to recover from.

The reset framework can still reject invalid, unacquired, unsupported or
removed handles before reaching a callback. This is not a claim that every
reset provider or every possible provider-lifetime violation is infallible.

Two assumptions remain explicit:

1. Runtime assertion reaches the supported CCU callback successfully.
2. AHB assertion retires outstanding MDMA accesses and clears the stale
   data-path state before the buffer is unmapped or reused.

The first is a provider/lifetime contract. The second is a hardware contract
supported by the prior H616 measurements, not guaranteed by a zero API return.
If assertion actually fails while DMA remains active, returning an error and
blocking later commands **does not protect the current caller buffer**. The
current cleanup still unmaps it. This is retained as an explicit assumption
counterexample, not classified as safe generic fault recovery.

After successful assertion, a failed deassertion is different: the engine is
already stopped, so its mapping can be released while the IRQ remains disabled
and controller-register accesses are suppressed.

## Validation of this revision

| Check | Result |
|---|---|
| `NandStreamingRecovery` | All 26 expected outcomes: 15 positive, six mutation and five assumption configurations |
| Assertion failure assumption | Read and write cases detect unsafe unmap when failed assertion leaves DMA active |
| Actual DMA/reset helper bodies | 16 direction/chip/soft-reset/deassertion cases, plus repeated mapping reuse |
| Exact assertion-error propagation | 16 combinations of direction, active/inactive DMA and four error codes; all preserve the error and block further MMIO/commands |
| Active-error negative controls | Eight of those 16 error cases deliberately observe the unsafe-unmap boundary rather than claiming safety |
| Source regression suite | All 16 jobs pass on the digest above; executable inputs unchanged during the run |
| Probe/lifecycle | 1,273 cases and 86,486 assertions, including missing reset and initial assertion failures |
| Cross-builds | ARM64 GCC and ARM32 LLVM `W=1` object builds pass at each changed prefix, patches 20 through 25 |
| Patch checks | Recovery patch passes checkpatch without warnings or errors; all 25 sign-offs and sent revision notes preserved |

The source tests run extracted, unchanged candidate bodies under ASan/UBSan.
The standard suite also checks historical hardware captures against their
original hashes; this is evidence replay, not a new board test. Local reports
are `results/tlc-ea6rxs4j/summary.json` and
`results/suite-rvdx7yod/summary.json` (generated results are not published).

```sh
python3 run_suite.py --module NandStreamingRecovery
python3 run_suite.py --candidate-source /path/to/sunxi_nand.c --source-only --workers 4
```

Sashiko's general technical and false-positive guidance, subsystem routing,
locking, MM allocation, MMIO-accessor and build prompts were checked against
the actual reset provider/framework and DMA callers. There is no dedicated
NAND prompt. The remaining finding is the explicitly scoped active-DMA/reset
failure assumption above, not an observed error from the supported CCU.

## Hardware evidence is unchanged

No fresh hardware run was performed for this reset-error policy change. The
[680-read implementation capture](H616-STREAMING-IMPLEMENTATION.md) remains
associated with its original driver digest, `183c5ff0ad786560110def4fd836bb42b40ff5eb884f1d633496c79e3d17f282`.
Those observations cover the successful assertion path, which is unchanged,
but cannot establish safe recovery from an assertion that fails to stop DMA.
No old capture or its source identity has been relabeled as this revision.
