# Bit-accurate randomizer and policy composition

The lifecycle models establish when the ordinary-format scrambling policy is
selected. This extension connects that policy to seed selection, control
register updates, marker compensation and exact bytes. It supplements the
symbolic stream models and the existing C arithmetic tests.

No kernel implementation is changed by these checks. They execute extracted
source helpers on the host, not a running kernel or physical NAND device.

## Arithmetic

`NandRandomizerMath` defines the implemented 15-bit recurrence explicitly:

```text
normalize: state = seed & 0x7fff
advance:   state = (state >> 1) |
                  (((state bit 0) XOR (state bit 1)) << 14)
byte mask: low eight bits of state, then advance eight times
```

The model uses bit positions and the recurrence rather than relying on the
polynomial name in a source comment. `NandRandomizerBits` compares the numeric
form against a separate sequence of fifteen bits whose first two bits provide
the feedback. Its full-domain configuration checks normalization and one
transition for every 16-bit input seed. Three smaller configurations check
24 successive transitions around zero and the 15-/16-bit boundaries. Byte
ordering is checked separately. Wrong taps, missing normalization and reversed
byte ordering must violate their named invariants, including at initialization.

`NandRandomizerPhase` starts with each of the 128 page seeds from the hash-pinned
Allwinner BSP. It advances fifteen bits and then 512 or 1024 bytes, checking
the corresponding ECC table entry observed from the driver. It also compares
four exact output bytes from the driver's `sunxi_nfc_randomize_buf()`. Thus the
table/phase check is not merely a comparison of two copies of a seed table.
Offsets of fourteen and sixteen bits are negative controls.

These are finite exhaustive checks over the stated domains, not a machine-
checked theorem about arbitrary-length C executions. The separate C suite
also checks all 65,536 seeds for zero-, one- and eight-bit advances against an
independent bit-array oracle.

## Policy and actual source observations

`run_randomizer.py` reuses the marker test's source extractor. It executes the
unchanged attach, Toshiba initialization, final ops selection (when present),
seed/configure/enable/disable, protected-OOB and randomized-buffer helpers.
It records observations at helper boundaries, without inserting observers in
the extracted bodies. MMIO storage, geometry setup and the underlying buffer
transport are fixtures. Register definitions and the older/H6-family enable
masks are checked against the supplied source before compilation.

The 500 directed cases vary:

- Older and H6/H616 register profiles, including their different enable bits.
- Plain, early-scrambled, late-scrambled and explicit vendor-format policies.
- 512-/1024-byte ECC steps and 32/64/128/256 pages per eraseblock.
- Page zero, eraseblock boundaries and the 128-entry seed-table wrap.
- Marker/non-marker protected bytes, good/bad marker bytes, and transport
  success or injected timeout.

This is not the full Cartesian product. Geometries exercise helper contracts;
they do not claim that the Toshiba part or H616 supports every combination.
Each controller retains its control register across the sequence, including
randomized-to-plain transitions. The host checker verifies continuity across
all records, including the boundaries of the TLC wrappers.

Independent inputs use the pinned BSP page seeds and a Python bit-array LFSR,
not the driver's seed-selection or stepping functions. Reads start from the
expected decoded USER_DATA representation of an independently specified image.
Writes are checked against that representation and the corresponding on-media
bytes. Ordinary bad-block markers remain plain; the vendor format leaves them
scrambled. Neighboring protected bytes must remain unchanged by compensation.
This tests compatibility separately in each direction, not just a round trip.

`NandRandomizerPolicy` checks the captured policy, selected page/ECC seeds,
control words, input/output bytes and both transport wrappers. The wrappers
must configure the correct seed, enable only for randomized formats, propagate
transport failure and return with randomization disabled. Control bits outside
the seed/enable fields are preserved. Bit 31 is represented separately from
the lower 31 bits because TLC's integer implementation is signed 32-bit; the
model does not discard that bit.

Eight altered-observation controls cover stale marker policy, wrong seeds,
wrong enable bits, clobbered control fields, corrupted read/write markers,
enablement retained after a failed transfer, and swallowed transport errors.
These are checker-sensitivity tests, not eight newly found kernel defects or
eight compiled driver mutations. Sanitizer and coverage builds must produce
identical observation files. Results retain source, helper, generated-fixture,
BSP, observation and model hashes; source coverage includes uncalled helpers
from the shared extractor rather than claiming every extracted path ran.

## Initialization and hardware boundaries

This trace runner explicitly invokes final ops selection. The actual
scan/manufacturer/BBT ordering remains covered by `run_lifecycle.py` and
`NandDeferredInitialization`; it is not proved by calling that helper here.
Their hypothetical late-SLC/subpage assumption counterexample remains valid.
The runner does not change a probed chip's requirements during ordinary I/O.

The arithmetic model does not choose when hardware reloads or advances the
LFSR. H616 NORMAL-command restart, page-mode direction differences and partial-
word read tails remain in `NandMeasuredStream`/`NandMeasuredToggle` and their
[captured hardware checks](HARDWARE-EXTENDED-20261104.md). The new policy model
does not simulate BCH decoding or the NAND bus. Older-controller reload timing,
unmeasured silicon and arbitrary concurrent failures are not established by
passing this extension. No new board measurement is claimed.

## Running

```sh
# Pinned submission: preserve the known late-marker regression as a control.
python3 run_randomizer.py --download

# Candidate: require correct policy and bytes in every case.
python3 run_randomizer.py --candidate-source /path/to/linux/drivers/mtd/nand/raw/sunxi_nand.c

# Exhaustive one-step arithmetic and small multi-step configurations.
python3 run_suite.py --module NandRandomizerBits
```

`--source-only` runs the sanitizer/coverage captures and independent host
oracle without claiming a TLC run. `--case` filters TLC configurations but
still executes the complete source matrix. Parse errors, crashes, timeouts
and unrelated invariant failures are never accepted as passing controls.

The pinned source retains 50 mismatching marker cases in this matrix; its
policy, read and write failures are each checked explicitly in TLC. A candidate
must have zero mismatches. `run_suite.py` includes the new runner, and the CI
template contains a dedicated job; this does not claim hosted CI execution.

## Validation record, 2026-11-05

The checked v11 driver has SHA-256
`159e0501b8906e74cd0a192724d61ddcab08a644842686e664db098f39151153`.

| Check | Result |
| --- | --- |
| Bit-level models | 4 positive configurations and 3 arithmetic mutations matched |
| Candidate phase/policy models | 10 positive configurations and 10 negative controls matched |
| Pinned historical phase/policy models | 22 expectations matched, including separate policy/read/write regression witnesses |
| Candidate source observations | All 500 policy cases and 256 ECC-table/byte-stream records matched the independent oracle |
| Existing initialization/marker models | 71 deferred-init and 20 marker expectations still matched, including the explicit late-SLC assumption failure |
| TLC result classification | Three unit tests passed; unrelated errors and missing counterexample evidence are rejected |

The positive model runs reached every required stage action. Source GCOV
reported 132/145 lines and 44/55 branches in the extracted functions; geometry,
engine-selection and unused shared-helper paths are not fully covered by this
focused runner. Broader source tests remain necessary. The 18-job source and
recorded-hardware replay suite also passed. These checks found no additional
driver defect beyond the already retained historical marker regression and
documented assumptions.
