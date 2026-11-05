# H616 write aborts, PIO timeout reuse and ordinary-kernel verification

These are fresh physical measurements of the current 25-patch v10 driver,
not a relabeling of the earlier read-only captures. The submission tip is
`8ceb5c5576dd25eb00373ec63c770dc2f9c7e18c`; hardware builds used the integration
tip `51601eff4029f1f47d75253bcbec6369decfe0c6`. Both contain identical
`sunxi_nand.c` bytes with SHA-256:

```text
102b942b18d37e37f6f6dd605233da79149e2fbff61f38daf25acde75f0ce676
```

No production driver change was made for these tests. Diagnostic hooks were
installed only in a disposable build copy. After testing, removing the hooks
reproduced the source hash above, and the ordinary integration Image was rebuilt.

The [sanitized capture](../hardware/h616-request-faults-20261105.json) contains
register observations, comparison results and restoration checks. It contains
no existing flash bytes, flash-content hashes, private paths or board access
details. The original backups, images and detailed logs remain private.

## Physical scope and safeguards

The device is an H616 with Toshiba TC58NVG1S3H NAND: 256 MiB, 2048-byte pages,
128-byte OOB and 128 KiB eraseblocks. The tested format uses BCH32/1024 with
Allwinner variable user-data lengths: eight protected bytes in the first
step, zero in the second, and an eight-byte unprotected tail.

The board booted a diagnostic image into RAM. UBI was explicitly detached;
the module rejected the new fault operations while UBI was still attached.
No UBIFS/JFFS2 filesystem was mounted. The module requires raw-I/O privilege,
validates this geometry and partition layout, and locks both the NAND chip
and controller while running a request.

Only physical eraseblock 7, an authorized expendable secondary-SPL backup
block, was erased or programmed. Fresh independent raw data+OOB backups of
all four SPL partitions were verified before that phase. A bounded existing
UBI block was also snapshotted, read-only. The test runner restores the
scratch block in its cleanup path; a failed recovery instead pins the
diagnostic allocation/module and requires independent recovery boot.

### Raw-backup discrepancy retained

Before any destructive operation, an initial raw MTD/module comparison
disagreed by one bit in an SPL page outside the writable block. That stopped
the test. 128 subsequent alternating 512/1024-byte-command reads of that page
agreed. A later full-backup attempt also failed comparison; that attempt did
not retain its failed module payload, so its differing location is unknown.
The snapshot helper now saves rejected payloads for future investigations.

Three further complete SPL reads agreed with the fresh MTD dumps and the
independent module backup. One complete, unmodified matching read became the
verified backup; no bytes were patched to make comparisons pass. The initial
rejected observations remain in the private evidence archive. These raw-read
differences are not classified as a driver bug or conclusively attributed to
the NAND array: raw reads bypass ECC, and the root cause was not established.

## Pre-confirm DMA write aborts

Operation 30 calls the actual DMA page-write callback with its caller buffer.
A one-shot test hook returns `-ETIMEDOUT` after the real MDMA command launch,
at delays of 0, 1, 2, 5, 10, 20, 50 and 1000 microseconds. This is an injected
error return, not a physically wedged DMA-provider test. Production abort,
AHB reset, cleanup and error propagation execute unchanged.

An observation counter at explicit PROGRAM CONFIRM issue checks that the
failed callback does not reach `nand_prog_page_end_op()`. Independent raw
readback also checks that the target remains erased. After each abort, a
normal write programs distinct data and metadata. Its raw representation is
compared with the independent BCH/LFSR oracle, followed by PIO and DMA reads
using their respective OOB representations.

| Observation | Result |
|---|---|
| Abort trials | 32: four repeats of each delay, on distinct pages |
| Controller busy at the injected fault | 28 trials |
| Callback result | `-ETIMEDOUT` in every trial |
| Explicit PROGRAM CONFIRM during aborted callback | None |
| Aborted target raw data+OOB | Entirely erased in every trial |
| Actual caller buffer, balanced DMA prepare/cleanup, memory guards | All pass |
| MDMA status/count immediately after reset and after 100 ms | Zero |
| Subsequent normal write, raw oracle, PIO and DMA readback | All pass |

The source allocation is retained for the 100 ms observation interval.
Unchanged source memory cannot detect late device **reads**, so this is not
an independent proof that write-side DMA bus accesses have retired. The
successful-reset retirement contract remains explicit. No NAND array program
or erase was interrupted after confirmation, and no power-loss test was done.

## PIO timeout cleanup and the first following request

Operation 31 runs the actual PIO page callback and withholds IRQ delivery
before either a NORMAL data command or an ECC-special command. The selected
wait is forced to expire after the configured short delay. Unlike the DMA
hook above, this retains the actual timeout status recheck, interrupt mask,
IRQ drain and event acknowledgement path. The diagnostic IRQ disable is
balanced separately from the driver's own disable/enable operations.

It immediately calls the next actual page callback on a distinct page, or
waits 1 ms first. Both PIO and DMA followers are tested. There is no
module-level reset, idle wait or context restoration between those callbacks,
and no discarded first read. The first follow-up data and OOB are compared
in full with independently verified reference pages. When the initial
callback succeeds through its status recheck, its data and OOB are checked too.

| Observation | Result |
|---|---|
| Trial matrix | 256: four repeats, two fault stages, two follower paths, two gaps, eight delays |
| First callback returned an actual timeout | 105: 96 NORMAL, nine ECC-special |
| First callback succeeded through status recheck | 151 |
| Controller busy at timeout-cleanup observation | 96 |
| Late command-complete flag before follow-up | 97 |
| Controller busy at follow-up observation | Zero |
| First follow-up data/OOB, path counters and memory guards | All pass |
| First/follow-up failed-ECC counts | Zero |

This validates the measured finite late-completion cases. It does **not**
establish safe reuse after a permanently stuck PIO command, or after an old
command finishes during the following request: none was still busy at the
follow-up observation. The model's late-producer counterexamples must not be
removed based on these results.

## Fresh ordinary-kernel smoke test and restoration

All four SPL partitions and the bounded UBI block matched their verified
pre-test raw backups after restoration. The diagnostic module was unloaded.
The board then RAM-booted the ordinary current integration Image, with no
diagnostic symbols in the running kernel and no probe module loaded.

Three 64 MiB MTD reads completed in 4.10, 4.13 and 4.13 seconds, approximately
15.5-15.6 MiB/s. These are smoke-test timings, not a controlled comparison
against earlier kernels. ECC accounting increased by nine corrected bitflips
and zero failures. There were no controller timeout/reset errors or kernel
warnings. A fresh raw scratch-block read still matched the backup.

Finally, normal boot was restored and its ordinary UBI attachment checked.
No persistent kernel, boot image or SD-card contents were changed.

## Model, replay and review checks

- `NandProgramRecovery`: all 46 expected outcomes matched.
- `NandRecoverySequence`: all 57 expected outcomes matched.
- `NandStreamingRecovery`: all 26 expected outcomes matched.
- Public replay checks the exact 32/256 matrix, successful restoration and
  ordinary-kernel observations. Twenty-four deliberately corrupted captures
  must be rejected, including wrong DMA/PIO paths and non-idle MDMA states.
- The bridge's install/remove round trip restored the exact current driver
  hash. Pinned-v9 unit tests also reject unsupported fault hooks without
  modifying its source.

The 129 model outcomes include deliberately failing mutations and hardware
assumption counterexamples; they are not 129 proofs of unconditional safety.
No model semantics or earlier captures were changed. The prior full suite
already passed 73/73 jobs on this driver hash. The fresh source/hardware suite
passed all 16 jobs with this new capture and unchanged executable inputs.
Its local report is `results/suite-wl3g899d/summary.json`; the three targeted
model reports are `results/tlc-nefmpuuk/summary.json`,
`results/tlc-sb2qm3u2/summary.json` and `results/tlc-mbrqde2g/summary.json`.
Generated results are not published.

```sh
python3 run_suite.py --module NandProgramRecovery
python3 run_suite.py --module NandRecoverySequence
python3 run_suite.py --module NandStreamingRecovery
python3 run_suite.py --candidate-source /path/to/sunxi_nand.c --source-only --workers 4
python3 run_hardware.py
```

Sashiko's general technical guidance, false-positive guide, locking, memory
allocation, MMIO-accessor and alignment prompts were checked against actual
callers and diagnostic error paths. There is no dedicated NAND prompt. No
additional production-code defect was confirmed by this run.

Unverified physical contracts remain: failed AHB assertion with active DMA,
arbitrarily late bus transactions, permanently stalled PIO, older NFC/external
DMA variants, different chip geometries, multi-chip hardware, and interrupted
confirmed array operations. These limits remain visible in the models and
should not be inferred away from a passing H616 experiment.
