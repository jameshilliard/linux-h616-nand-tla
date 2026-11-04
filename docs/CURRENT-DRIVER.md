# Current sunxi driver verification

This audit checks the v10 draft's driver, not just the original v9 snapshot.
The checked sunxi branch is `submit/sunxi-nand-fixes-v10` at
`91bf468161998d95266ea7df0dad5359e81e9508`, based on NAND `nand/next` at
`30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85`. Its driver SHA-256 is:

```text
16f1c5563953b58696f1e90404836fdb3f6ce62a196dbedf1354a0b3860b0b68
```

The pinned files under `reference/` are unchanged. The draft contains the
split-address and clock-error fixes in addition to the earlier series.
The page, low-level, lifecycle, codec, composed-request, discovery and
lifetime runners accept `--candidate-source`; unrelated core, codec and
historical inputs remain pinned. The separate core fixes are
tested independently, not silently included in the sunxi candidate.

## Areas exercised

| Driver logic | Source execution and model coverage |
| --- | --- |
| Probe, resources, clocks and IRQ/DMA initialization | Allocation/defer/initialization faults and unwind; lifecycle and probe models; actual scan/cleanup helpers |
| Chip selection and cached clock | Actual selector and `exec_op`, repeated failures/retry, same-rate geometry changes, check-only; `NandSelection` |
| Command parsing and raw I/O | Actual NAND-core parser, split address/data instructions, SRAM boundaries, ready/busy, polling and transfer faults; command/media models |
| Timing | Independent minimum-period and lookup oracles, each dominating constraint, rounding, EDO boundaries and check-only; timing/negotiation models |
| ECC geometry and callback selection | Strength/step normalization, OOB fit, small-page restrictions, PIO/external-DMA/internal-DMA selection, allocation failures and format opt-in |
| User-data lengths and layouts | Allocation, BBM reservations, partial tail bytes, PIO/DMA packed registers, defensive invalid lengths; batch/OOB models |
| Randomizer and on-media format | Seed derivation, byte identity, BBM transforms, mixed PIO/DMA writers/readers, legacy/BSP comparisons and real software BCH composition |
| ECC and pattern interpretation | Status snapshots, pattern precedence, counter caching, generic erased-page rereads, vendor spare heuristics and decoded metadata preservation |
| Page/subpage/OOB requests | Data-only and requested OOB, scrambled/plain writes, tail/cursor handling, error cleanup, single confirm and no write replay |
| DMA and IRQ ownership | Three backends, mapping/directions, descriptor limits/publication, batches, completion/timeout/drain/recheck and read fallback statistics |
| Core-to-driver boundaries | Persistent-media MTD requests/BBT traffic, ONFI/ID discovery into attach, retained-user/unbind lifetime witnesses |

The three complementary driver runners extract and execute all **90 driver
functions unchanged**. Their union reaches **1,427 of 1,431 source-mapped
lines** and **701 of 712 branch edges**. GCOV uses a separate unsanitized
build so sanitizer-generated branches do not inflate the counts. The
ASan/UBSan executions pass separately. This is host-fixture coverage, not
coverage from running the kernel on a board or a proof of every code path.

Coverage reports must have the same full driver digest and matching
per-function body hashes. `coverage_report.py` rejects a mixture of v9 and
v10 reports instead of combining different source-line numbers.

## New directed cases

- **1,636 write-path fault executions:** PIO/DMA page, OOB and subpage
  writes, both scrambled/plain and requested/absent OOB. No error may
  confirm a partially failed write or leave ECC/randomizer/DMA ownership.
- **114 OOB transport cases:** every transport failure point in selected
  generic batches and vendor finish paths; exact error propagation,
  bounded buffer writes and deferred vendor-statistics publication.
- **16 DMA page/OOB selection-error cases:** two error codes across both
  DMA modes and length layouts; no transfer starts after failed selection,
  and a subsequent retry succeeds.
- **24 low-level chip-switch cases:** all three capability backends,
  equal/different rates and both chip-select entries; repeated errors do
  not change registers/cache, check-only performs no I/O, and successful
  retry restores the correct chip context.
- **2,096 interface cases:** the earlier mixed timing matrix plus each
  individually dominating field, both EDO conditions, the 30 ns boundary
  and a slower rounded clock. The 960,120 direct timing lookups remain.
- **9 callback-selection cases:** no DMA, external DMA and internal DMA,
  with large pages and the legacy small-page configuration.
- Additional checks cover contiguous small-page data/OOB, non-word OOB
  tails and invalid user-data encodings without register modification.

Existing byte-identity, randomizer, erased-predicate, attach, lifecycle,
core-composition and real-BCH tests also run against the candidate source.
See [VALIDATION.md](../VALIDATION.md) for recorded results, rather than
assuming a newer driver passed because this document exists.

## Chip-selection model

`NandSelection` exhaustively explores three serialized requests over two
or three chips, including two chips sharing a rate. Each request may be
check-only, `exec_op` or the direct DMA-write selection path. Clock-change
success/failure is nondeterministic on each request; failed attempts do not
consume a permanent failure state, so later requests can retry.

It checks cached/hardware rate consistency, unchanged state after failure,
check-only purity, correct chip context before transfers, and bounded
progress. Five deliberate regressions must fail: caching a failed rate,
ignoring the error in either caller, omitting geometry updates on cache
hits, and programming during check-only. Twelve positive configurations
and one provider-contract counterexample supplement those mutations.

The model represents rates as profiles and geometry as chip identities.
It is not a formal refinement proof of the C code. Actual selector tests
check the concrete spare, timing, chip-select and ready/busy registers.
Positive cases assume NAND-core serialization and that a failed clock
change preserves the previous physical clock. Dropping the latter
contract produces an explicit counterexample; it is not a newly observed
sunxi clock-provider defect.

## Remaining gaps

The subsequent [hardware audit](HARDWARE-CONTRACTS.md) reproduces finding 11
against this exact candidate: direct H616 extra-OOB transfers above 1024 bytes
conflict with the documented ten-bit counter. The earlier coverage counts do
not establish correctness of the old wider-counter fixture. No fix for this
new finding has been applied to the candidate by the validation repository.

The four missed source lines are timing-lookup error returns. With the
current positive tables and admitted timing ranges, the preceding maximum
calculation already guarantees an encoding exists. The lookup's rejection
behavior is tested separately; the tables were not corrupted simply to
force these paths. Extreme arithmetic outside those ranges is not proved.

The remaining branch edges include unused capability/helper combinations
(the non-reserved legacy layout, invalid DMA user lengths and a NULL
write-tail cursor), parser-excluded instructions/zero address cycles,
those timing errors, and expanded fixture `IS_ERR` guards. They are
reported, not represented as complete branch coverage.

The confirmed open-MTD-reference/unbind lifetime issue remains unresolved.
Neither a registration reference nor a model assumption drains retained
users before sunxi cleanup releases hardware resources. Multi-chip probe
unwind also needs to be covered by any eventual lifetime fix.

Physical DMA stop, outstanding bus retirement, late command events, MMIO
ordering, real clock-tree behavior, physical manufacturer-specific read retry and
hardware BCH/randomizer behavior remain explicit boundaries. Software
BCH and sunxi-tools fixtures are not captured hardware golden images.
No board access, NAND writes or hardware validation was done for this audit.

The subsequent [refinement checks](REFINEMENT-VALIDATION.md) connect actual
command and manufacturer callback bodies, exercise retained MTD users through
lookup and I/O, and refine reset/register and byte/cache-line assumptions.
They extend the source evidence without establishing the physical guarantees.
