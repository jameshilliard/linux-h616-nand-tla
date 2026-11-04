# Candidate fixes and regression checks

The default runners still check the pinned submission and reproduce its
known counterexamples. Candidate options read separate source files; they
do not replace `reference/` or change the published baseline's hashes.
Candidate results are not evidence that the baseline has been fixed.

## Draft patch organization

| Finding | Draft branch | Change |
| --- | --- | --- |
| 1, split address suboperations | `submit/sunxi-nand-fixes-v10` | Iterate over the returned cycle count, using the start offset only to index the input. |
| 2, failed clock setup | Same sunxi branch | Return the clock error before changing controller registers or the cached rate; propagate it to callers. |
| 4, read-retry OOB accounting | `submit/rawnand-read-retry-oob-v1` | Copy OOB and advance its output pointer only after the retry decision. |
| 5, BBT marker-scan error | `submit/rawnand-bbt-scan-error-v1` | Return the scan failure before publishing an incomplete table. |
| 6, BBT read transport error | `submit/rawnand-bbt-read-error-v1` | Return non-ECC transport errors from either selected copy, retaining existing ECC recovery. |
| 7, timing fallback error | `submit/rawnand-timing-recovery-v1` | Check controller and chip reset errors, preserve an earlier error, and skip chip RESET after controller fallback fails. |
| 9, IRQ bank dispatch | `submit/sun6i-dma-irq-banks-v1` | Separate fixes for the physical-channel index and partial final bank, using the current per-controller bank width. |
| 10, cyclic callback lifetime | `submit/sun4i-dma-callback-lifetime-v1` | Serialize IRQ handling with termination, retain terminated cyclic descriptors, and drain IRQ/tasklet execution before freeing them. |
| 11, direct-buffer command count | `submit/sunxi-nand-fixes-v10` | Cap direct commands at 1024 bytes as well as SRAM capacity; share the limit with the parser. |

These are local draft branches, not public prerequisite references. The
NAND-core and provider branches apply independently of the sunxi OOB series.
The sunxi series remains based on NAND's `nand/next` at `30daaf3e7897`;
the DMA branches use `dmaengine/fixes` at `6628e7d4c6b2`.

The four NAND-core fixes are independent one-patch branches on the same
NAND base. Each exports one patch without a cover message and has no patch
prerequisites. The original `submit/rawnand-core-error-handling-v1` branch
is retained unchanged. Splitting preserved each diff, commit message,
authorship, Fixes tag and sign-off.

## Running candidate checks

With the appropriate branch checked out in a separate kernel repository:

```sh
python3 run_lowlevel.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c --expect-count-fixed
python3 run_c.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
python3 run_codec.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
python3 run_lifecycle.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
python3 run_composed.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
python3 run_discovery_boundary.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
python3 run_lifetime_boundary.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
python3 run_transport.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
python3 run_transport.py --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c --count-boundary --expect-count-fixed
```

For the earlier v10 draft before finding 11 was fixed, omit
`--expect-count-fixed`: the old source must still reproduce the oversized
H616 count witnesses. This explicit switch prevents silently inferring the
expected outcome from whatever implementation happens to be loaded.

For the combined core branch, use `run_core.py --candidate-dir` without
`--fix`. For an individual core branch, select just its expected fix:

```sh
python3 run_core.py --candidate-dir ../linux/drivers/mtd/nand/raw --fix read-retry-oob
python3 run_lifecycle.py --candidate-nand-base ../linux/drivers/mtd/nand/raw/nand_base.c
```

Check all four committed branches without switching checkouts:

```sh
python3 run_split_core.py --kernel-dir ../linux \
  --ref read-retry-oob=submit/rawnand-read-retry-oob-v1 \
  --ref bbt-scan=submit/rawnand-bbt-scan-error-v1 \
  --ref bbt-read=submit/rawnand-bbt-read-error-v1 \
  --ref timing-recovery=submit/rawnand-timing-recovery-v1
```

Each run expects only the selected fix and retains witnesses for the
unfixed core bugs. It records the resolved Git commit and source hashes.
For the separate provider branches:

```sh
python3 run_irq_regression.py --kernel-dir ../linux --ref submit/sun6i-dma-irq-banks-v1
python3 run_provider_boundary.py --candidate-sun4i ../linux/drivers/dma/sun4i-dma.c
```

Use each source option only with its matching candidate branch. The IRQ
runner reads a committed revision, not the worktree. Its `--expect-bug`
option checks that the unpatched revision reproduces an incorrect event.
The runners record extracted source hashes and generated fixture hashes in
their result directories. Sanitizers and source coverage remain enabled.

The candidate NAND-core tests also cover the second BBT read of an
unversioned table, partial OOB error lengths, and successful timing fallback.
Aborting on a BBT transport error is deliberate: silently using an older
mirror need not preserve the newest bad-block entries.

The IRQ regression executes the current handler and register accessors for
eight-channel and one-channel banks. It tests 8, 12, 16, 24 and 32 channel
bounds, cyclic/non-cyclic events, and unused fields in a partial bank. This
cross-product includes boundary tests; it is not a claim that every
combination is a real SoC. The checked baseline produced 278 bad events in
368 cases; the candidate produced none.

The sun4i candidate checks include half/end IRQs, normal/dedicated channels,
NULL/non-NULL callbacks, repeated termination, an IRQ paused between channel
lookup and locking, and termination from inside a callback. There are 60
candidate schedules, compared with 24 baseline schedules and two retained
freed-callback witnesses. The fixture marks descriptors freed while keeping
storage available for checking; it does not deliberately dereference freed
host memory.

Only cyclic descriptors are retained until synchronization. Other collected
descriptors are freed immediately; any non-cyclic descriptor already being
used by a callback is on the tasklet's private list, not the channel lists
collected during termination. Thus ordinary NAND aborts do not accumulate
non-cyclic descriptors while waiting for a later channel synchronization.

`NandLifecycle` and `NandNegotiation` include positive checked-error variants.
`NandProviderBanks` covers both bank widths. `NandCyclicLifetime` explores
IRQ/tasklet/termination interleavings for one virtual channel, contrasting
retention until synchronization with immediate freeing. The model assumes
no new `issue_pending` between termination and synchronization, as required
by DMAengine. It does not prove physical-channel reuse across different
virtual channels, posted-MMIO completion, or DMA bus retirement.

## Findings not converted into patches

- **3: external DMA configuration failure.** Both supported sun4i/sun6i
  configuration callbacks return zero. The injected failure remains a
  conditional API robustness observation, not a reproduced provider bug.
- **8: open MTD references during unbind.** Unregistering does not drain
  retained users, while sunxi immediately releases their resources. A safe
  fix needs an explicit disconnected-state/resource-lifetime design, also
  covering multi-chip probe unwind. Merely suppressing bind attributes or
  adding a warning does not cover all these paths. No fix is claimed here.

Hardware/provider contracts in [FINDINGS.md](FINDINGS.md) remain explicit.
The candidate source checks and bounded models do not replace hardware tests
or establish that all possible concurrent interleavings have been checked.

## Independent sunxi probe fixes

Findings 12 and 13 now have standalone candidates on the pre-series NAND base:
availability filtering and deferring MTD publication until all NAND scans
finish. They do not depend on one another or on the OOB series.
[PROBE-VISIBILITY.md](PROBE-VISIBILITY.md#independent-candidate-fixes-2026-11-04)
records their commits, source checks and remaining boundaries.

`run_probe_fixes.py` checks both real diffs in both application orders, tests
the base, each independent fix and their combination, and requires four
deliberate regressions to fail specific assertions. Its temporary Git indexes
leave the kernel checkout and real index unchanged. For a candidate source
file directly, `run_lifecycle.py` accepts `--expect-available-children` and/or
`--expect-deferred-publication` together with `--candidate-source`; only enable
the expectation implemented by that source.
