# Current sunxi driver verification

## Current integration (2026-11-04)

`submit/sunxi-nand-fixes-v10` now ends at
`c95239dadd36c5d4c012c228ed4a95971c89b65a` on the same NAND base. The
23-patch series includes availability filtering and scan-before-publication
as patches 20 and 21, before the binding and randomized-OOB changes.
The current driver SHA-256 is:

```text
f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6
```

Fresh lifecycle checks pass 1,209 cases and 81,560 assertions with both fix
expectations enabled, under ASan/UBSan and a separate GCOV run. ARM32 LLVM
and ARM64 GCC `W=1` object builds pass. The [probe audit](PROBE-VISIBILITY.md)
records the corrected admission/publication behavior and partial-error unwind.
The generic NAND-core and DMA-provider fixes remain on separate branches.

The full-driver coverage totals below belong to the earlier source digest,
not this integration. They have not been recomputed for the new full source.
Model files, pinned sources and the earlier validation records are unchanged.
The retained-MTD/unbind lifetime issue remains unresolved.

## Earlier full-driver audit

This audit checks the v10 draft's driver, not just the original v9 snapshot.
The checked sunxi branch is `submit/sunxi-nand-fixes-v10` at
`ccdee2ee49862a1377d31f5c541ce9d5fab327df`, based on NAND `nand/next` at
`30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85`. Its driver SHA-256 is:

```text
7e2525a0097a156a7a4e952a073c03ca19872f1c417a42387f15d5b85081a50a
```

The pinned files under `reference/` are unchanged. The draft contains the
split-address, clock-error and normal-command byte-count fixes in addition
to the earlier series. The 21-patch draft is distinct from the posted v9.
The page, low-level, lifecycle, codec, composed-request, discovery and
lifetime runners accept `--candidate-source`; unrelated core, codec and
historical inputs remain pinned. The separate core fixes are
tested independently, not silently included in the sunxi candidate.

## Areas exercised

| Driver logic | Source execution and model coverage |
| --- | --- |
| Probe, resources, clocks and IRQ/DMA initialization | Allocation/defer/initialization faults and unwind; lifecycle and probe models; actual scan/cleanup helpers |
| Child admission and publication | Status/presence/order matrix; actual core lock acquisition during the next scan; wrong-CS/timing command-launch witnesses; admission/publication models |
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
- **144 direct-buffer round trips:** A10, A23 and H616 with independent
  generation contracts, including short/exact/large transfers, scrambling
  and page seeds. Every command and SRAM copy stays within 1024 bytes.
  Eighteen first/second/third-command failures preserve error propagation
  and the untouched suffix. The zero-length call issues no command.
- **Four composed large-OOB cases:** read and write compare the complete
  1872-byte H616 tail and 1928-byte legacy tail under synthetic geometries
  accepted by the real attach code. The unchanged v9 still reproduces
  the H616 count-loss counterexamples.
- **48 independent raw-stream sequences:** actual read/write wrappers and
  buffer helpers, two seeds, plain/scrambled data, partial command failure,
  subsequent requests and zero-length cleanup. Three deliberately injected
  command-reload alternatives pass round trips but fail stored-byte identity.
- **52 DMA aperture cases:** exact/crossing 32-bit range endpoints in both
  directions on all three backends, descriptor extent/alignment and explicit
  mask setup. Two above-mask injections demonstrate the dependency on the DMA
  API contract; they are not newly reachable driver defects.
- **87 persistent length-register sequences:** wide-to-narrow PIO/DMA
  transitions, zero metadata, all packed-word boundaries and nonzero logical
  starts; adjacent-register sentinels and legacy no-MMIO behavior checked.
- **28 DT child-admission cases:** seven status variants, both child positions
  and presence/absence; 16 unavailable-child scan witnesses remain in the
  current candidate as well as the baseline.
- **Probe/I/O scheduling:** actual core lock acquisition for the first
  published chip does not exclude the next scan's detector. Twelve low-level
  schedules launch a first-chip read with the second chip's CS/timing. These
  are explicit host schedule witnesses, not a real concurrent kernel test.

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
The probe/publication tests show why the serialization assumption cannot be
extended to identification of a subsequent child: that path omits the shared
mutex while the previous child's MTD is already usable.

## Remaining gaps

The [hardware audit](HARDWARE-CONTRACTS.md) reproduced finding 11 against the
previous candidate (`91bf468161998d95266ea7df0dad5359e81e9508`). The current
candidate includes `2fb2a7138fa1`, which caps normal commands independently of
the advertised SRAM size. Explicit `--expect-count-fixed` runs require exact
transfers, rather than accepting the baseline's counterexamples. Fresh coverage
reports for this source digest still reach the counts given above; coverage
alone does not establish that a hardware fixture is accurate.

A10/A23 valid-request and register contracts are checked against the older
BSP rather than borrowing H616's counter width or automatic event clearing.
The A10 manual has no register chapter and the A23 manual was unavailable.
H616's advertised 8 KiB SRAM capability conflicts with the manual's description
of two 1 KiB banks; physical extent/aliasing remains unmeasured. The corrected
driver tests use the stricter 1 KiB normal-command and RAM0-bank bounds.

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
Two independent probe defects were unresolved in this pre-fix snapshot:
unavailable DT children are scanned, and per-child publication allows the
next scan to race with normal I/O. They occur before the OOB changes too.
They are corrected by the integrated candidate described above. The
[probe audit](PROBE-VISIBILITY.md) distinguishes baseline witnesses, model
policies and implemented fixes from the separate unbind issue.

Physical DMA stop, outstanding bus retirement, late command events, MMIO
ordering, real clock-tree behavior, physical manufacturer-specific read retry and
hardware BCH/randomizer behavior remain explicit boundaries. Software
BCH and sunxi-tools fixtures are not captured hardware golden images.
In particular, the large-tail randomizer tests check software seed handling
under a continuous-stream contract; hidden silicon reload behavior between
commands has not been measured.
No board access, NAND writes or hardware validation was done for this audit.

The subsequent [refinement checks](REFINEMENT-VALIDATION.md) connect actual
command and manufacturer callback bodies, exercise retained MTD users through
lookup and I/O, and refine reset/register and byte/cache-line assumptions.
They extend the source evidence without establishing the physical guarantees.
The [stream/aperture/context extension](STREAM-APERTURE-CONTEXT.md) adds
independent checks at the raw-command and request-reuse boundaries, and lists
the remaining hardware, lifecycle, concurrency and refinement gaps by priority.
