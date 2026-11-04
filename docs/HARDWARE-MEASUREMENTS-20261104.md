# Real H616 NFC measurements, 2026-11-04

Tested hardware: H616, Toshiba TC58NVG1S3H, 256 MiB, 2048+128-byte pages,
128 KiB eraseblocks, two 1024-byte BCH-32 steps. The running 7.3.0-rc3
integration image is **not the v10 submission**. Its physical observations
are supplied separately to source-body tests of the v10 candidate.

The [capture](../hardware/h616-capture-20261104.json) contains register values,
comparison results and hashes of synthetic images or XOR keystreams. No
existing NAND contents, connection details, private paths or keys are included.
The diagnostic source digest identifies the module used for the final tests:
[the source at commit 27c962b](https://github.com/jameshilliard/linux-h616-nand-tla/blob/27c962b/hardware/nfc_probe.c).
Subsequent blank-line/enum formatting does not change its C tokens. Host-side
backup preflight and transport error handling were subsequently tightened;
the capture is not represented as a new hardware run of those tool revisions.

## Safety and measurement method

The root-only module takes the NAND core's chip-then-controller mutexes,
checks geometry and controller, holds an MTD reference, waits for idle and
drains previous IRQ handlers before custom operations. Unbinding NAND was
forbidden: this does not solve the generic MTD-unbind lifetime issue.

All 512 SPL pages were backed up off-board with data **and OOB**. Independent
512-byte and 1024-byte command reads agreed with each other and with raw
`nanddump --bb=dumpbad`. Kernel-side guards limited erase/program to block 7,
the second eraseblock of the fourth backup SPL partition. Outside-block
requests were rejected even with writes armed. No other partition was an
experimental write target.

Synthetic pages were programmed once per erase cycle. After both destructive
runs, the backup block was restored in ascending page order, including its
original OOB/markers; raw comparisons of **all four SPL copies** passed. The
module was unloaded. A subsequent 32 MiB MTD read completed in 2.09 seconds
without new controller/ECC errors in the kernel log.

## Count, SRAM and events

| Test | Observation |
| --- | --- |
| 14 CNT values from 0 through 2049 | Readback retains ten bits; zero transfers 1024 bytes. 1025 transfers one byte, 1536 transfers 512. Prefix and untouched-tail checks agree. |
| SRAM +0x400 and +0x800 | Both documented 1 KiB banks retain independent patterns; their previous contents were restored. |
| Command-done status | Writing zero or the ready/busy event bit does not clear command-done; writing command-done does. Events are visible with interrupts masked. |

This confirms the transfer-count fix's hardware premise, not the existence of
an 8 KiB aperture or all command/IRQ/reset ordering guarantees.

## Normal-command randomizer: previous assumption disproved

216 read measurements cover three page contents, three seeds, eight chunk
sizes, ordinary operation, explicit reseeding and page-size mode. All match
the measured model. The capture retains 72 distinct XOR-keystream digests.
These normal-command probes use ECC disabled and the documented LSB-first
randomizer setting; MSB-first behavior was not tested.

| Configuration | Observed behavior |
| --- | --- |
| Write, RANDOM_SIZE=0 | Restart from the configured seed on every command. |
| Write, RANDOM_SIZE=1 | Also restart on every command. |
| Read, RANDOM_SIZE=0 | Restart on every command. |
| Read, RANDOM_SIZE=1 | Continue by the requested byte count between commands. |
| Read count not divisible by four | Final requested byte has no randomizer XOR; preceding bytes use the expected keystream. |
| Write count not divisible by four | All requested bytes are randomized. |

The read-tail effect persisted for 12 lengths with pipeline off/on and with
`memcpy_fromio()` versus explicit aligned `readl()` accesses: 48 checks. It
is not merely a byte-copy artifact. These observations do not establish its
RTL cause or behavior in untested configurations.

Sixteen synthetic write/read experiments independently checked stored bytes.
All writes matched command-local restarts. In block-size mode four aligned
chunkings round-tripped; four non-word-sized chunkings did not. In page-size
mode none of the eight multi-command round trips matched: reads continued
while writes restarted, with the additional non-word read-tail exception.

The old continuous-stream H616 assumption is therefore superseded. Earlier
passing host cases are conditional software tests, **not silicon evidence**.
A10/A23 behavior remains unmeasured; do not copy H616 observations to them.

This is not yet a new production-page corruption report. The tested 2 KiB+128
layout uses aligned data commands and an eight-byte tail, and its complete
physical images matched. Other admitted layouts and multi-command extra-OOB
formats need a caller/format audit before selecting a driver change. Merely
switching RANDOM_SIZE is not an established fix, given its measured asymmetry.

## BCH and protected OOB

Eight native-encoder pages matched independent GF(2^14) polynomial division
and LFSR encoding across **all 2176 bytes**. The format uses eight protected
user bytes in step zero, none in step one, 56 parity bytes per step, and eight
unprotected tail bytes. Each codeword restarts the page seed; the tail starts
from that seed separately. Data, protected OOB and tail readback matched.

Twelve host-encoded pages were written raw, including 1/16/32 data bitflips,
1/32 protected-OOB bitflips, 16/32 parity bitflips, second-step errors, a
selected 65-bit uncorrectable pattern and one unprotected-tail bitflip.
Both direct PIO `ECC_OP` reads and native page callbacks were exercised:

- Through 32 protected errors, data/user bytes and correction counts matched.
- The selected 65-bit pattern set the first step's ECC error bit and the
  native failed-statistic delta. The callback returned transport success;
  MTD interprets the failed delta. Zero return alone does not imply correction.
- The tail bitflip was returned unchanged without an ECC correction count.
- An erased page returned all-ones, both pattern bits/IDs, and no error delta.
- PIO status was captured before further commands. Native post-callback status
  was often cleared by raw-OOB reads; it cannot replace the earlier snapshot.

These are finite BCH-32 examples, not exhaustive error-pattern or analog
retention validation. Native callbacks may fall back internally; invoking
them does not prove every transfer used DMA. The explicit PIO path has no
such ambiguity. Above-strength errors are not universally detectable.

## Validation integration and limits

`NandMeasuredStream` adds 23 TLC expectations: aligned cases, single-write/
page-read cases, four incompatible-format/tail witnesses and three incorrect
hardware-model mutations. Its keys are symbolic positions; the independent
bit-level replay checks captured digests rather than assuming unique key bytes.

`run_hardware.py` replays 170 captured checks without accessing a board.
The low-level runner adds 44 actual v10-source cases under the measured H616
profile. Historical continuous/reload-only cases are now labeled conditional.
This is not a mechanical whole-driver-to-silicon refinement proof.

Still unmeasured: genuinely hung DMA/reset bus retirement, controlled late
events, stronger BCH modes/other geometries, large-OOB media, power-loss and
program limits, and older controllers. Additional TLC states cannot establish
those hardware guarantees. Never inject hung DMA into this live-root setup.

The audit checked Sashiko technical patterns, false-positive, MMIO/accessor
and alignment guidance from the local prompt checkout, plus the actual NAND
core locking/helper implementations. That checkout has no dedicated MTD/NAND
prompt. This does not establish kernel-series submission readiness.
