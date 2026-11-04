# Whole-request DMA, ECC and recovery traces

`run_request_refinement.py` connects unchanged NAND-core request code to the
actual sunxi page/OOB entrypoints, command parser, DMA preparation, event wait,
abort and cleanup helpers. Separate translation units exchange values through
adapters; they do not cast between incompatible host representations of kernel
structures. Source hashes identify every extracted function.

This closes a specific earlier gap: the page caller now chooses the sequence
of DMA/recovery helpers, instead of a test calling those helpers in a presumed
order. It remains bounded source execution, not a proof of every C execution.

## Inputs and checks

The matrix has 810 source executions over three format profiles and external,
register and descriptor DMA backends, with PIO comparisons. It uses synthetic
2 KiB pages with 256-byte OOB; other geometries are exercised by the existing
layout, batching, codec, low-level and OOB-pipeline runners.

- Actual core data+OOB reads/writes and OOB-only reads reach page callbacks.
- Actual subpage-read entrypoints handle a nonzero, non-step-aligned request.
  These direct helper calls are distinct from core request dispatch.
- Mapping failure, first ECC-wait timeout and two consecutive ECC-wait
  timeouts exercise DMA-to-PIO read retry and write failure handling.
- Two later requests reuse the controller without clearing its registers or
  media, changing rate and then chip selection.
- ECC errors, with and without a following normal-command failure, exercise
  error classification, statistics rollback, return lengths and reuse.
  The vendor DMA data-only path classifies the saved result without a raw
  reread; an OOB request additionally exercises raw-read failure handling.

An independent raw image initializes the read tests. Its page seed comes from
the hash-pinned vendor table; a slow bitwise LFSR produces the physical bytes.
Successful writes must match 18 complete independently encoded images, not
merely round-trip through the same writer/reader implementation. Parity in
this request fixture is explicitly opaque, not a BCH encoder. The separate
codec runner covers real software BCH and sunxi-tools images.

The source execution observes ownership after mapping, DMA launch, waiting,
aborting and unmapping, along with page entry/return, normal-command errors,
program begin/confirm and final request results. `NandRequestTrace` separately
reconstructs mapping and caller state and compares it with these snapshots.
It checks balanced maps, no unmap with active device ownership, clean IRQ/ECC
return state, no replay after failed writes and bounded/exact return lengths.

All 810 inputs are accounted for in 139 distinct behavioral projections.
Eighteen TLC wrappers check those projections; seven altered-observation
controls must fail their named invariants. The controls detect a lost unmap,
wrong snapshot, unsafe unmap, leaked IRQ or ECC enablement, false short success
and a repeated program confirm. They are checker tests, not new kernel bugs.

ASan/UBSan and an independent GCOV build must produce identical trace, input
and stored-image bytes. Recorded digests are checked before reusing results.
The C assertions additionally compare buffers, guard bytes, ECC accounting
and reuse behavior. Deduplication does not omit any input profile.

## Concurrent ordering model

`NandRequestOrdering` explores two requests sharing a controller with separate
IRQ status-read, mask-read, acknowledge, mask-write and signal actions. Timeout,
IRQ drain, status recheck, abort and unmap can interleave with device events and
outstanding bus retirement. Both external and integrated DMA are represented.

The ten configurations include two positive contracts, six deliberately weak
hardware contracts and two omitted-drain mutations. Failed stop, unretired bus
accesses and old events arriving during a later request must expose their
specific safety failures. These are conditional counterexamples, not evidence
that physical hardware violates the stronger contract. IRQ draining alone
does not supply device retirement guarantees.

## Reproduce

Install the pinned checker and run the ordering matrix:

```sh
python3 run_ordering.py --download
python3 run_request_refinement.py
python3 run_request_refinement.py \
  --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
```

`--source-only` separates C execution from TLC. Check saved observations with
`--results results/request-RUN`; that mode verifies their recorded hashes and
the current independent input generator. A `--case` filter is diagnostic,
not a full validation run.

`run_suite.py --candidate-source PATH` runs the ordinary source matrices,
standard TLC catalog, dedicated probe/recovery/OOB/request traces and lifetime
checks against one candidate snapshot. It currently expects the v10 count-limit,
child-filtering and deferred-publication fixes; it is not a revision auto-detector.
The suite records executable input hashes and rejects edits during a run.
`--source-only` selects the fourteen ordinary C matrices without TLC.

## Remaining hardware and kernel boundaries

The request fixture supplies device payload movement, register side effects,
reset success, bus retirement and event delivery. The interleaving model varies
these contracts, but neither model establishes the real silicon behavior.
Physical BCH, hidden LFSR reloads, SRAM aliasing, cache/MMIO ordering, arbitrary
compound failures, real SMP/RT execution and full device-core teardown remain
outside this proof boundary. The known MTD resource-lifetime issue is described
in [FD-LIFETIME-CONFORMANCE.md](FD-LIFETIME-CONFORMANCE.md).

Physical randomizer validation needs byte comparisons against an independent
raw image, including protected metadata, parity and command boundaries. A
matching write/read round trip is insufficient. Controlled timeout tests also
need evidence of bus retirement and late events, not just a software IRQ drain.

If redundant boot partitions are used for destructive experiments, their names,
physical offsets, erase geometry, bad blocks and existing format must first be
verified on the running device. Take and verify complete raw data-plus-OOB
backups, establish a working raw restoration and independent recovery path,
and reserve one exact redundant region. Reformatting a backup is an intentional
loss of that boot copy until restoration; it must not touch the primary copy,
other backups, secure storage or unrelated boot images. Stop if the raw restore
method or target boundaries are uncertain. Private captures stay out of this
public repository. No destructive hardware test is part of these runners.
