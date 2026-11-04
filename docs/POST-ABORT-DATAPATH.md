# H616 post-abort data-path investigation

## Result

An active MDMA abort followed by the controller's **soft reset** can leave
the next transfer's first 1 KiB incorrect. This is not confined to ECC decoding:
raw reads can also return a circularly displaced block. Some ECC reads return
incorrect data with no failed-ECC count. Fresh row/column setup, a NAND-device
RESET and reprogramming the normal controller registers are insufficient.

The follow-up isolates the failure to the controller data path, rather than
the stored page or just the decoder. Clearing SRAM contents did not resolve
it. Asserting and deasserting the controller's **AHB bus reset** after soft
reset eliminated it in 36 interleaved trials; soft-reset-only aborts failed
19 of their 36 trials. That is a useful recovery candidate, not a universal
silicon guarantee or a production driver fix.

We have not identified a named internal register or reconstructed the RTL.
Residual buffer-position/packing state is consistent with the observations;
the exact hidden state and reset-domain implementation remain unknown.

## Test design

These are additional experiments following the immutable
[extended capture](HARDWARE-EXTENDED-20261104.md), using the same v10 driver
function bodies (SHA-256
`f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6`).
Diagnostic additions observe SRAM and expose the driver's owned reset handle;
they do not change the production page algorithms. The bus-reset experiment
uses `reset_control_assert()`, a 2 microsecond hold, and
`reset_control_deassert()`. It does not acquire a second reset handle or write
the CCU directly.

Three distinct synthetic pages use SHAKE256-derived main/OOB data. Native
writes are checked against independent BCH/LFSR physical encoding, then raw,
PIO, MDMA and public MEMREAD reads establish the baseline. Each trial primes
a third page, aborts a read of the first page, and reads either the same or
a different page. This removes the ambiguous byte identity of the earlier
periodic test pattern.

All tests booted a diagnostic kernel into RAM, with no flash-backed mounts
and no attached UBI devices. Only the authorized backup block was erased or
programmed. Fresh off-board NAND dumps and independent 512/1024-command reads
agreed before testing. Each matrix restored the block. Final independent
reads matched all four SPL copies, the module unloaded, and a normal 32 MiB
MTD read passed. No program/erase operation was aborted. DMA allocations
remained owned throughout fault injection and subsequent observations.

## Captured results

| Matrix / operation | Trials | Incorrect first subsequent reads |
| --- | ---: | ---: |
| First matrix: completed transfer then soft reset | 72 | 0 |
| First matrix: active abort then soft reset | 72 | 20, including 7 without failed-ECC accounting |
| Follow-up: soft reset, observe SRAM only | 12 | 5 |
| Follow-up: soft reset, clear SRAM contents | 12 | 8 |
| Follow-up: soft reset, another NAND-device RESET | 12 | 4 |
| Follow-up: soft reset, discard one 4-byte raw command | 12 | 0 |
| Follow-up: soft reset, discard one 64-byte raw command | 12 | 0 |
| Follow-up: soft reset, discard one 1024-byte raw command | 12 | 0 |
| Follow-up completed-transfer controls, all six operations | 72 | 0 |
| Bus-reset comparison: completed transfer then soft reset | 36 | 0 |
| Bus-reset comparison: active abort then soft reset | 36 | 19, including 8 without failed-ECC accounting |
| Bus-reset comparison: active abort, soft reset, AHB bus reset | 36 | 0 |

The matrices contain **396 trials** and **936 replayed read/SRAM observations**.
Counts are diagnostic observations, not calibrated failure probabilities.
Precise abort timing depends on the running kernel and controller phase.
The first matrix uses 0/1/10 microsecond launch delays; subsequent matrices
use 1/10 microseconds. Completed controls wait for both CMD and DMA completion.

In every incorrect full-page observation, the second 1 KiB was correct.
Every second full-page read matched. Retained DMA buffers stayed unchanged
and guards remained intact. These are finite observations, not proof of
arbitrary failed-engine retirement or a universal two-read recovery rule.

### What the bytes establish

- One raw first-matrix result is an exact 52-byte circular displacement of
  the requested page's first 1 KiB. A native DMA result has the same exact
  displacement. Other silent ECC failures are mostly a 48-byte displacement,
  with two additional altered bytes; do not describe every failure as a
  pure rotation.
- Different-page reads demonstrate that this is not simply the previous
  page being returned. It can corrupt the placement of newly requested data.
- Clearing both SRAM banks to a sentinel reads back correctly, but does not
  reliably make the subsequent page read correct.
- Eleven post-abort diagnostic commands modified SRAM outside their requested
  prefix. For example, a four-byte NORMAL read could change bytes 4 through
  51 while leaving bytes 0 through 3 untouched. Completed-transfer controls
  wrote exactly their requested prefix. This concerns internal SRAM, not a
  demonstrated out-of-bounds write to the host allocation.
- A dummy NORMAL read cleared the observed problem for the following page
  read, but the dummy itself could be corrupt. This supports a data-path
  state issue, without establishing a safe production flush sequence.

## Manual and source interpretation

The [H616 manual](https://www.scs.stanford.edu/~zyedidia/docs/allwinner/h616.pdf),
section 5.2.6.1, describes bit 1 of `NDFC_CTL` as self-clearing reset. It does
not establish that observing it clear proves every internal state is fresh
after an active abort. Sections 5.2.6.31–33 distinguish descriptor address,
buffer address and DMA count: offset **0x214 is the DMA byte counter**, not
an address. The capture's third abort register is that counter.

The pinned [vendor soft-reset helper](https://github.com/radxa/allwinner-bsp/blob/2045a3ca2a01f088c0314dc924bda59d154e363e/modules/nand/common1/phy-nand/rawnand/controller/ndfc_v1px.c)
preserves `NDFC_CTL` while setting its reset bit. Earlier experiments tested
that variation and a second settled soft reset; neither reliably prevented
the anomaly. NAND-device RESET and controller AHB reset are distinct
operations and must not be conflated.

The current driver abort path uses controller soft reset, then cleanup. Its
DMA-read error path can subsequently retry through PIO. This investigation
shows why a contract equating reset completion with correct subsequent data
is too strong. The test intentionally aborts a running read much earlier
than the driver's timeout; it does **not** demonstrate that ordinary healthy
traffic spontaneously reaches the same fault or that a genuinely hung
engine is recoverable with the tested sequence.

## Formal integration

`NandPostAbortRequest` composes recovery, retained ownership, fresh NAND
positioning, payload delivery, separate CMD/DMA events, ECC status capture,
copy-out, public success/failure accounting, and a second request. It uses
two symbolic steps of four distinguishable words, not 2048 literal bytes.
The independent replay checks the actual physical and logical byte strings.

The abstraction separates:

1. A reset-complete/quiet observation from DMA retirement.
2. Fresh public register/row/column configuration from internal data ordering.
3. Successful ECC status from correct delivery of the decoded bytes.

The hidden offset represents observed rotation; it is not a claim about
a specific internal pointer. Decode errors are abstracted separately. A
complete command clearing hidden state is explicitly parameterized, as are
the effects of a discarded read and bus reset. Weakening those contracts
must produce counterexamples rather than silently inheriting successful
hardware results as universal assumptions.

The 77 configurations cover PIO/MDMA, completed/aborted transfers, clean,
rotated and failed-decode data, six continuation policies, and weakened
recovery/retirement contracts. Rotation produces a `SoundSuccess`
counterexample despite valid completion events and successful ECC. Clearing
SRAM or resetting the NAND chip does not erase that witness. Quarantine
avoids publication, but is a modelled alternative, not an implemented policy.

This complements the existing source-helper and whole-request trace models.
It does not upgrade their assumed hardware effects into a full C-to-silicon
proof. Older controllers, other NAND parts, failed bus reset, outstanding
transactions from a genuinely wedged engine and write-abort recovery remain
outside this experiment's evidence.

## Reproduction and validation

Read the [diagnostic safety requirements](../hardware/README.md) before using
`hardware/run_reset_diagnosis.py`. The default, `--followup-matrix` and
`--bus-reset-matrix` phases have independent output files and restoration.
The public [capture](../hardware/h616-reset-20261104.json) contains only
synthetic buffers, deduplicated by SHA-256. All original flash backups, boot
images, board addresses and private logs remain outside this repository.

Offline checks:

```sh
python3 run_hardware.py
python3 run_suite.py --module NandPostAbortRequest
```

Replay validates every captured byte and the exact input-profile matrix;
negative controls reject tampered data, hidden failures, missing controls and
unverified restoration. The full candidate-source run passed all 66 jobs,
including 995 standard TLC configurations across 46 models (77 configurations
in the new model), the separate source/trace checks and hardware replay.
All executable input hashes remained unchanged throughout the run. Expected
counterexamples are required outcomes, not evidence that their unsafe policies
are correct. Twelve hardware-safety/replay unit tests also passed.

The Sashiko general technical-patterns and
false-positive guides, plus locking, I/O-accessor and alignment guidance were
checked at commit `feed8a17375278783d7277a5f1eedc32fa3230f9`.
There is no dedicated NAND prompt in that checkout.
