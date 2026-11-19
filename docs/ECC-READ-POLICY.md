# ECC read policy boundaries

The ordinary driver decides **one ECC step at a time**. The deferred
Allwinner randomized-OOB format must decide **once for the whole page**.
The two decisions use the same hardware status but do not assign it the
same meaning. A chip that requests NAND scrambling can still use the
ordinary format.

| Evidence | Ordinary format | Firmware format |
| --- | --- | --- |
| ECC error, with or without a pattern | Preserve the slot for a raw erased-step check after consuming the batch. Propagate raw-read transport failures. | Preserve decoded main and protected OOB, including on data-only reads. Record the exception and independently record any zero pattern. No ordinary raw reread. |
| Pattern without an ECC error | Fill main data on PIO or scrambled DMA; plain DMA preserves its delivered main bytes. Fill requested OOB with the slot's pattern. An all-ones step marks the page as having an erased chunk. | Record the pattern as an exception even if hardware suppressed the ECC error. |
| Neither flag | Use the corrected bytes and correction count. | Collect the count for a possible ordinary decoded-page result. |
| End of read | Only read the tail when OOB is requested; use the raw tail if any step was erased. | All-zero physical steps take priority, even with matching erased spare: zero-fill and count one failed step per step. Otherwise an exception plus erased spare yields an erased page. Other pages keep the decoded data and only read unprotected OOB on request. |

`NandEcc` dispatches before both batch consumption and page finish. Its
independent oracle uses the original logical inputs, not the decision
bitmaps, and checks the returned byte categories, error statistics, tail
choice and termination. Intermediate invariants check raw-reread ownership
and retained firmware evidence before a whole-page fill can hide a mistake.

```sh
python3 run_ecc_read_policy.py --download
python3 run_c.py
```

The 23 positive TLC configurations cover all six combinations of error and
pattern states over a three-step page, one-slot and three-slot snapshots,
both OOB choices, ordinary scrambled chips and per-step raw reread outcomes.
DMA buffers are initialized independently of the injected status. A six-step
input crosses a packed correction-count word: each read caches four entries
and selects a byte by the hardware slot modulo four. Eleven negative
controls must violate their expected invariants, including choosing the
firmware path just because scrambling is enabled, dropping either firmware
dispatch, overriding plain DMA's delivered pattern bytes, reusing an old
counter word at slot four, and preferring erased spare over an all-zero page.
Three additional controls check the pattern-ID register access itself:
an unneeded read, a duplicate read of a valid zero, and skipping a pattern
that coincides with an error on a firmware page. The ordinary path needs
the ID only for patterns without errors; the firmware path needs it for
every pattern. The expected number of reads per batch is zero or one.

### Subpage requests

For the ordinary format a physical DMA transfer can include steps before the
requested data. These steps must not change the reported ECC statistics,
trigger a raw erased-step reread, or change a good read into an error. Skipping
their status must not renumber the hardware slots for the rest of the batch.
The firmware format deliberately does something different: its subpage hook
uses the full-page read so the page classification sees all steps.

`NandSubpage` compares the implementation's step-range arithmetic with an
independent byte-interval intersection oracle. For each six-step geometry it
checks all 171 nonempty byte ranges, including one-byte reads, unaligned
endpoints, an interval starting at the last slot of a five-slot batch, and
intervals reaching the next batch's slot zero. The corrected sum, failed
count, maximum, raw-reread set and return are checked separately. The ECC
hook's successful result is a maximum, not a sum; NAND core reports
an uncorrectable step through the increase in the failed count.

The negative controls distinguish an unrequested bad step causing
`-EBADMSG`, an unnecessary raw reread failing with `-EIO`, and a healthy
request inheriting an earlier step's high correction count. Other controls
skip earlier steps during the correction pass but still reread them for raw
error checking, renumber the status slots, truncate the final partial step, or
restrict firmware classification to the requested steps. These are bounded
single-attempt checks: a DMA transport error can trigger a PIO retry; the
model does not imply that the error necessarily reaches userspace. Data
copying, MMIO and NAND read-retry modes are not simulated. Firmware range
tests use a non-erased spare and no pattern flags; the pattern and spare
outcomes are checked by `NandEcc` above.

```sh
python3 run_ecc_read_policy.py --case subpage
```

To compare the exact driver revisions under review:

```sh
python3 check_ecc_read_policy.py --first-refactor early/sunxi_nand.c \
    fixes/sunxi_nand.c firmware/sunxi_nand.c
```

The checker prints digests for the supplied files. It checks the ordinary
decision order, DMA pattern write guard and packed-count access, as well as
the range arguments, both ECC pass guards and the firmware full-page entry
point. It checks that the format chooses whether to read the pattern ID.
Pass `--expect-prefix-bug` to check the historical unguarded implementation.
The checker requires the two early returns to be the only edits to the
ordinary batch and finish bodies and compares the three firmware decision
bodies to the reference used by the C harness. It intentionally fails closed
on source drift. It does not execute the candidate or certify a new
implementation of a helper.

The range and pattern-read checks passed on the fix-only driver with SHA-256
`a2ad1b837a00df0a89aeefe550f6817193a50e542091731bf71d55086efd944a`
and its dependent firmware variant,
`a77f3dab060faf9783590d51f55a867c0524835288ec06417d83b531d7169c2f`.

The earlier revision, before the subpage range guards, has SHA-256
`bf8d0e51a53878dfdeaaa615a56708d939984fbf5c76f95d051218a3f42d4b0c`;
the deferred driver has
`027789043609599ca9561b019785a194cd186d9a14024e3063052df8aee32f6a`.
Separate host runs with GCC and Clang compared 9,216 ordinary traces across
the H6 and A10/A23 layouts and 430 firmware traces (64 with both error and
pattern) byte for byte before and after refactoring. The 430 observations
were replayed through `NandOobPipeline` in 119 TLC configurations, including
11 expected counterexamples. These were not tests on physical NAND.

These are bounded status and control-flow checks. `raw`, `ones`, and
`decoded` are categories, not calculated physical NAND bytes; `normal`
tail means the ordinary non-erased path, not necessarily that the chip
scrambles. The raw erased outcome uses one representative corrected bit.
The model does not predict which hardware flags a physical page will
produce, exercise MMIO, or inject every possible NAND transport failure.
For actual byte comparisons see the [OOB pipeline](OOB-PIPELINE-CONFORMANCE.md).
