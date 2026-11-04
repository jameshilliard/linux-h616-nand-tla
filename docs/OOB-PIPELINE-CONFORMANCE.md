# Randomized OOB/ECC pipeline observations checked by TLA+

`NandOobPipeline` connects the actual Allwinner-format batch and finish helpers
to an independent byte-level checker. Earlier models check symbolic format and
ECC contracts, while the codec runner checks real software BCH. This extension
joins decoded metadata capture, accumulated ECC status, erased-page decisions,
raw parity/tail transfers, representation conversion and final accounting.

It is a bounded check of source-helper observations, not a proof of arbitrary
C executions or a model of the physical BCH decoder.

## Source and input boundaries

`run_oob_pipeline.py` runs the unchanged driver bodies through `run_c.py`.
The fixture invokes the actual user-data allocator, batch planner, status
snapshot, Allwinner batch reader, spare classifier and finish helper. It
supplies the decoded USER_DATA bytes and hardware ECC/pattern registers, then
records accumulated status after each batch. A transport-boundary observer
records each raw OOB read's column, destination, length and return value.
No observer is inserted into a kernel function body.

The script supplies the batch-caller loop. It does **not execute the whole
page entrypoint** in this trace matrix. Other source tests execute those page,
subpage and OOB callers. Hardware decoding, DMA movement, MMIO effects and NAND
transport remain explicit fixtures.

Independent input generation takes the 128 page seeds from the hash-pinned
vendor `ndfc_base.c`, not from the driver being tested. A bit-at-a-time LFSR
oracle produces the metadata, parity and tail masks. Decoded protected bytes
and physical raw protected bytes are deliberately different, so classification
from a raw reread cannot silently stand in for classification from corrected
metadata. The status/byte combinations test helper contracts; not every
synthetic combination is asserted to be physically reachable.

The reference classifier implements the two documented vendor policies:

- Legacy: the reconstructed eight-byte prefix is erased, or the first four
  bytes are erased at the special first/127th-page positions for 1024-byte ECC.
- Variable-user-data H6/H616: the marker is erased and at least nine of the
  ten reconstructed prefix bytes are erased.

Missing protected prefix bytes are padded with `0xff`. The source and checker
give the all-zero-page result precedence over the erased-page heuristic.
These are compatibility heuristics, not proof that NAND was never programmed.

## Bounded matrix

There are 430 source executions per driver version:

| Dimension | Selected inputs |
| --- | --- |
| Legacy layout | One 512-byte step; 2, 4 and 16 1024-byte steps; four protected bytes per step |
| Variable layout | 2, 4 and 32 1024-byte steps; protected bytes packed into the first step |
| ECC status | Good, zero pattern, one pattern, error, zero plus error, one plus error; mixed and first/last-error pages |
| Request | PIO/DMA and data-only/requested OOB |
| Spare prefix | Erased prefix, each marker/prefix position changed, multiple changed positions and legacy short-prefix cases |
| Seed/page | Pages 0, 1, 63, 64, 127, 128 and 255 with selected 32/64/128/256-page blocks |
| Tail | Zero, one or thirteen bytes after 56 parity bytes per step |
| Failure | Every raw-read call in the selected legacy/variable layouts, plus success and an unreachable failure index |

This is a directed matrix, not the full Cartesian product of all dimensions.
The observed runs contain 607 raw-read calls, 52 injected transport failures,
56 all-zero results and 164 erased-page results. Initial correction/failure
counters are nonzero, so error paths must preserve prior statistics rather
than merely produce zero.

## Independent checks

The Python reference compares every captured and final OOB byte, every raw
read, both statistics counters, the return value and main-buffer class.
TLA+ separately reconstructs the operation stages and checks the same
observations, including intermediate batch counters and exception bitmaps.

The raw-read plan is defined as maximal contiguous regions in the complement
of the protected-byte set. It does not reproduce the driver's coalescing loop.
Each successful raw read changes only its unprotected region. A failed read
stops processing without conversion or publication of accumulated ECC stats.

Normal PIO parity bytes receive their existing parity-stream transform;
normal DMA parity remains in its raw representation. Both paths transform
the unprotected tail using the page-seed stream. Captured protected bytes are
preserved except when the whole-page zero/erased policy deliberately replaces
them. A data-only request still captures the metadata needed for classification
but does not read raw parity/tail bytes.

The runner accounts for all 430 records in 108 wrappers of at most four traces.
Eleven altered-observation/input controls must fail their specific invariants:
capture corruption, wrong batch status, wrong erased decision, protected-byte
clobbering, wrong tail/parity phase, changed DMA parity, statistics publication
on error, swallowed transport errors, raw reads through protected bytes and
loss of zero-page precedence. These are **checker sensitivity tests**, not
eleven newly discovered driver bugs or eleven compiled kernel mutations.

ASan/UBSan and the separate GCOV execution must produce identical observations.
Reports retain the driver, function-body, combined fixture, input-vector and
observation hashes. Reusing results requires the binary vectors to match the
current independent generator and the source runner's recorded input digest.
TLC checks termination of each bounded trace and requires all seven stage
actions in the unfiltered run. Caches memoize input-derived values and read
bounds keyed by trace/cursor; their mathematical definition is the identity
operator. Immutable layout data is retained in the state, and each raw region
is bound once before updating bytes, avoiding repeated lazy set evaluation.

## Reproduce

Run against the pinned submitted source:

```sh
python3 run_oob_pipeline.py --download
```

Or provide a candidate without replacing the pinned source:

```sh
python3 run_oob_pipeline.py \
  --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c --download
```

To separate source execution from model checking, use the printed directory:

```sh
python3 run_oob_pipeline.py --source-only
python3 run_oob_pipeline.py --results results/oob-pipeline-RUN
```

`--case` selects a wrapper/control for diagnosis; it does not validate the
unselected inputs in TLC. A parser error, timeout, JVM failure, sanitizer error
or unrelated invariant violation is never a passing negative control. The
dedicated runner's counts are separate from the standard configuration catalog.
The CI template has a separate OOB-pipeline job; this does not assert that it
has been installed or run by GitHub Actions.

## What this still does not establish

The trace matrix uses synthetic raw parity and explicit decoded-byte inputs.
The complementary `run_codec.py` checks Linux BCH against an independent
polynomial encoder, real parity/correction vectors and sunxi-tools page images.
Those software checks still do not prove the controller's parity padding,
hidden LFSR reload/continuity across command boundaries, or compatibility with
an independently captured hardware/BSP golden image.

The [hardware audit](HARDWARE-CONTRACTS.md) and
[stream checks](STREAM-APERTURE-CONTEXT.md) retain those limitations explicitly.
This extension does not address retained-MTD/unbind lifetime, real SMP/RT
schedules, arbitrary compound failures, NAND wear or power-loss behavior.
No board access, NAND write or kernel implementation change is required.
