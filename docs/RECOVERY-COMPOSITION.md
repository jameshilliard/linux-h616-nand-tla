# NFC recovery and measured whole-request composition

This extension covers the five follow-ups to the
[post-abort investigation](POST-ABORT-DATAPATH.md). It adds host validation,
not a production recovery implementation or another hardware experiment.
The candidate driver has SHA-256
`f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6`.
The pinned posted v9 source remains unchanged and is checked separately.

This is the original investigation record. The subsequent
[reviewability update](REVIEWABLE-V10.md) fixes the vendor-tail decoder,
separates stop from bus-reset retirement assumptions, and expands the matrix
to larger pages and later DMA batches. The counts and pre-fix descriptions
below remain specific to the earlier snapshot.

## Added coverage

| Area | New checks | Boundary retained |
| --- | --- | --- |
| Recovery sequence | IRQ mask/drain, soft stop, bus-reset assert/deassert, unmap, clock/context restore, retry and failure closure | Bus reset and quarantine are candidate policies, not current Linux code |
| SRAM/data placement | Distinct request bytes, rotated delivery, writes outside a short prefix, partial command errors, second bank and next request | Symbolic placement represents observed outcomes, not reconstructed RTL |
| Measured randomizer composition | Whole data/OOB requests, protected regions, command-local tail writes, hardware and software readers, independent stored-byte oracle | H616 block mode; parity in this request fixture is opaque |
| Write recovery | DMA retirement, page-buffer staging, confirm rejection/acceptance, busy/status failure, torn media, NAND reset and readback | Interrupted-program effects and neighboring-page preservation are explicit NAND-part contracts |
| Compound failures/reuse | DMA timeout followed by PIO timeout, failures in later recovery, subsequent same/different-chip requests, fresh context | Finite sequences, fair scheduling and specified device contracts; not arbitrary-length proof |

Four models add 149 configurations:

| Model | Configurations |
| --- | ---: |
| `NandRecoverySequence` | 41 |
| `NandSramPlacement` | 24 |
| `NandProgramRecovery` | 46 |
| `NandMeasuredPage` | 38 |

Positive cases check safety and termination. Negative cases must violate the
specific named invariant, rather than merely fail to parse or time out.
Required actions are checked for reachability across the successful cases.
The separate C runner executes 256 actual NAND-core requests in four geometry
builds, with identical sanitizer and GCOV observations.

## Recovery sequencing

`NandRecoverySequence` keeps IRQ-handler draining separate from retiring the
device producer and outstanding bus accesses. A late device event may arrive
at any recovery stage; masking an interrupt does not delete that event.

The `source` policy represents the current abort/cleanup/PIO-read-retry shape.
It preserves witnesses for ignored stop failure and for successful retry with
incorrect post-reset data. It does **not** invent an abort on a PIO error:
the PIO path returns without calling the DMA abort helper. If its timed-out
command remains active, subsequent reuse produces a separate counterexample.
These producer/late-event cases remain conditional hardware contracts.

The `bus-reset` policy explores a proposed stronger sequence. Reset assertion,
deassertion, clock restoration and context restoration can fail independently
at the first or second request. Failed recovery must not resume I/O. Unproven
DMA retirement must keep the mapping owned. The abstraction currently uses
one retirement guarantee for both stop and bus reset; it does not establish
that bus reset can rescue a failed stop on a particular provider.

The `quarantine` policy closes the controller without returning its potentially
active memory. This models safety, not a complete resource-reclamation design.
Neither alternative is implemented in the production driver by this work.

Cases also remove the assumed bus-reset data-path effect, retain old events,
skip context restoration, and deliberately reopen after failed reset. These
must reproduce their corresponding safety violations. A passing 36-trial
physical bus-reset experiment is not silently promoted into a guarantee.

## SRAM placement

`NandSramPlacement` uses two banks of eight distinct symbolic words. Clean
requests write exactly the requested prefix. Dirty requests may rotate data
or place additional words beyond that prefix. The second bank is an explicit
preservation contract; no host-memory overrun is inferred from SRAM changes.

Counts 1, 3, 4, 7 and 8 cover short/full commands and partial-error copy rules.
Source behavior, SRAM clearing, bus reset, a discarded read and refusal to
continue have separate cases. Recovery effects are parameters: weakening a
discarded-read guarantee must fail, just as SRAM clearing does not fix the
observed hidden data-path state. The second request uses different data to
detect stale contents. This is not a claim that all real failures are pure
rotations or that one discarded read always recovers hardware.

## Source-connected measured requests

`run_measured_requests.py` reuses the actual-source extraction and value
adapters of `run_request_refinement.py`. It changes only the hardware fixture:

- H616 NORMAL block-mode commands restart the randomizer at each command.
- A non-word read does not XOR its final requested byte.
- The configured seed register is not an internal phase counter.
- A selected soft reset of active MDMA leaves the next first-block delivery
  rotated by 52 bytes, matching one observed outcome class. The code does not
  assume this happens after every real reset.

The fixture rejects untested page-mode/MSB-first configurations. Hardware
capture digests, generated-source digests, extracted-function hashes and
observed artifacts are recorded in each result. Existing historical fixtures
remain explicitly conditional; this runner does not rewrite their meaning.

Each geometry build executes 64 core calls:

1. Twelve plain/scrambled/vendor-format PIO/DMA writer-reader combinations,
   each with a write, an `AUTO_OOB` read and a `PLACE_OOB` read.
2. Four timeout profiles, each with a seed write, a failing or recovered read
   and two later reads without reinitializing controller/media state.
3. Six failed-write cases and six subsequent writes to a different erased
   page: rejected tail, partially accepted tail, or accepted confirm whose
   completion times out, for both PIO and DMA writers.

An independent bitwise LFSR and hash-pinned vendor seed table construct the
expected physical image. It checks all stored data/user/parity/tail bytes,
then compares returned data, protected free OOB and extra OOB separately.
Parity is the existing opaque `0x5b` codeword contract, not a hardware BCH
encoder; independent real BCH checks remain in the codec/hardware runners.

`AUTO_OOB` does not expose the unprotected extra tail. The `PLACE_OOB` calls
are therefore essential: a successful AUTO read alone would miss the new
tail mismatch. A deliberately wrong continuous-write oracle must disagree
with the expected multicommand physical image. C guard bytes, return lengths,
mapping snapshots, confirm counts and subsequent reuse are also checked.

## New conditional source finding: large vendor-format tails

The driver admits the synthetic 2048+2048 geometry with BCH-32/1024. Its vendor
layout has eight protected user bytes, 112 parity bytes and a **1928-byte
unprotected tail**. The count-limit fix makes the writer transfer that tail
as 1024 and 904-byte NORMAL commands. Measured H616 block-mode hardware starts
the page-seeded stream again on the second command.

The reader takes a different path:

```text
nand_do_write_ops
  -> sunxi_nfc_hw_ecc_write_page[_dma]
  -> sunxi_nfc_hw_ecc_write_extra_oob
  -> sunxi_nfc_randomizer_write_buf
  -> sunxi_nfc_write_buf: multiple commands, hardware restarts each time

nand_do_read_ops (PLACE_OOB)
  -> sunxi_nfc_hw_ecc_read_page[_dma]
  -> sunxi_nfc_allwinner_oob_finish_read
  -> sunxi_nfc_allwinner_oob_read_unprotected
  -> raw parity/tail read, then one software randomize_buf over the entire tail
```

The software reader advances continuously past byte 1024. Consequently the
second tail segment differs even though the core returns success and full
data/OOB lengths. Main data and protected metadata remain correct. All four
PIO/DMA writer-reader combinations reproduce this mismatch in the extracted
candidate source under the measured hardware contract. The 2049-byte OOB
synthetic case reproduces it too. `NandMeasuredPage` has the corresponding
`measured-page-vendor-software-*` counterexamples.

This is a confirmed source/format mismatch **for that admitted geometry and
hardware contract**, not a demonstrated failure on a known large-OOB NAND part.
The real 2048+128 part's vendor tail is eight bytes and passes, as do 256-byte
OOB cases. Before changing production format handling, identify applicable
physical geometries and check compatibility with existing vendor images and
older NFC generations. No universal randomizer change is implied.

### Separate non-word read-tail case

For scrambled non-vendor format with a synthetic 2049-byte OOB, the final
hardware-randomized NORMAL read has a non-word count. Its last byte remains
in randomized on-media form, while the request reports success. This is a
caller-level reproduction of the measured read-tail quirk, not another error
in the software LFSR. A real supported part with this geometry is not claimed.
The vendor software-reader path does not have this particular non-word effect
because its raw read runs with hardware randomization disabled.

## Write-side recovery limits

`NandProgramRecovery` separates transfer ownership, staged data/user/parity/tail,
PROGRAM CONFIRM acceptance, array changes, ready/status and readback. Errors
before confirm do not program media. A rejected or partially transferred tail
must not be confirmed. An accepted confirm followed by timeout can leave all,
some or none of the staged pieces programmed; returning an error does not
promise unchanged storage. NAND reset discards staging, not committed changes.

Negative controls detect confirmation before all pieces arrive, replay after
a partial write, interpreting timeout as success, unsafe unmap and treating
reset as rollback. A failed NAND reset prevents the model from assuming the
chip is ready for another read. Power-loss effects outside the target page
are separately weakened to demonstrate the neighboring-page assumption.

The actual-source fixture models one concrete accepted-confirm outcome: the
whole staged page reaches media before its completion times out. TLC explores
other partial-array outcomes. This does not prove any physical part's torn
program boundaries, program budget, retention or power-failure behavior.

## Reproduce

```sh
python3 run.py --download
python3 run_suite.py --module NandRecoverySequence
python3 run_suite.py --module NandSramPlacement
python3 run_suite.py --module NandProgramRecovery
python3 run_suite.py --module NandMeasuredPage
python3 run_measured_requests.py
python3 run_measured_requests.py \
  --candidate-source ../linux/drivers/mtd/nand/raw/sunxi_nand.c
```

Without a candidate source, the runner uses pinned v9 and checks 128/256-byte
OOB. A candidate adds 2048/2049-byte OOB and must include the count-limit fix;
`--spare` selects a single diagnostic geometry. These are host-only scripts.
The full candidate suite also runs capture replay and the older regressions.

## Review and remaining evidence gaps

The scope audit used the Sashiko technical-patterns, callstack,
false-positive, build, locking, I/O-accessor and alignment guides at
`feed8a17375278783d7277a5f1eedc32fa3230f9`, plus upstream kernel
`review-core.md` (not included in that vendored subset). No dedicated MTD/NAND
guide exists there. Actual helper bodies and callers were inspected; the new
tests compile those bodies unchanged. No kernel build, schema check, new
hardware run or full patch-series readiness review is claimed.

The false-positive check distinguishes reachability explicitly: these are
valid host requests against attach-admitted geometries, but the larger and
odd OOB cases are not identified physical NAND devices. Neither the layout
checker nor the core excludes the constructed requests. `AUTO_OOB` excludes
the extra tail from the public copy; `PLACE_OOB` demonstrably reaches it.
The precise write/read chunk mismatch follows from executed production helper
bodies and measured H616 semantics, rather than an assumption that similarly
named APIs have matching stream behavior. This is not a NULL/lifetime finding.

Still unproved: failed-engine bus retirement, autonomous late device events,
bus-reset failure effects on real hardware, other NFC generations, exact
post-abort internal RTL, interrupted-program/erase guarantees, full weak-memory
and SMP/RT behavior, and an unbounded C-to-TLA+ refinement. Those are evidence
boundaries, not closed merely by adding more TLC configurations.
