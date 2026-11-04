# Extended H616 NFC measurements, 2026-11-04

## Scope and provenance

These measurements use the **current sunxi NAND v10 driver bodies**, not the
older integration used for the [first capture](HARDWARE-MEASUREMENTS-20261104.md).
The uninstrumented source SHA-256 is
`f22a4d77dfdad49266529f459a9a14ffbdf4ded6159517e4f663d02c83f682e6`.
It matches the local 23-patch v10 draft and the hardware integration branch.
The draft tip at measurement time was
`c95239dadd36c5d4c012c228ed4a95971c89b65a`; this is provenance, not a claim
that this draft has been submitted or merged.

The test kernel adds a diagnostic bridge and four call counters; it does not
change the normal driver algorithms or suppress fallback. The bridge selects
the actual PIO or MDMA page implementation explicitly. Counter deltas distinguish
PIO entry, PIO write entry, DMA preparation and DMA cleanup. The installed
callback is tested separately. A preparation attempt alone would not prove
that no fallback occurred: the checks also require no PIO entry on DMA reads.

The platform is H616 with a Toshiba TC58NVG1S3H, 256 MiB NAND, 2048+128-byte
pages and 128 KiB eraseblocks. The installed layout uses BCH-32 per 1024 bytes,
8 protected user bytes in the first step, none in the second, and an 8-byte
unprotected tail. The test kernel boots into RAM, with no flash-backed mounts
and **no attached UBI devices** during fault injection. Checking the device
tree alone was insufficient: U-Boot can add a UBI node to the supplied DT.
The recovery module checks actual UBI attachment before accepting operations.

Only the explicitly expendable last block of a secondary SPL copy is written.
Every matrix starts from independently compared raw backups, restores the full
raw main+OOB block in `finally`, and verifies its bytes. Final module reads and
independent `nanddump` reads matched all four original SPL copies. The module
was unloaded; an ordinary 32 MiB MTD read then passed. The kernel build-tree
instrumentation was removed and its source hash checked before rebuilding.
The board was then returned to its normal SD-booted image, without the probe.
Original flash data, boot images, credentials, endpoints and local paths are
not included in this repository.

The probe evolved during this investigation: operations 21/24 were added for
the actual subpage callback and retained-buffer inspection; operation 25 and
reset variants followed. The final extension primes all four user-length words
for the separate context matrix. Earlier captures are retained unchanged;
they were not silently regenerated with the final probe. The native bridge
and its driver call counters were unchanged throughout these runs.

## Results

| Area | Measured cases | Result |
| --- | --- | --- |
| Native page paths | 64 pages, alternating PIO/MDMA writes; PIO, MDMA and installed-callback reads | Physical main+OOB matched independent BCH/LFSR encoding on every page; all decoded main/user/tail checks passed; no DMA fallback |
| Seed/column/direction | 64 page seeds × 12 columns × 4 block/page and LSB/MSB modes | All 3,072 reads matched the independent stream oracle |
| Randomizer enable transitions | 24 reads and 24 writes, chunks 4/31/64/513 | Read page-mode phase pauses while disabled; block reads and writes restart per command; non-word read tail remains plain |
| BCH strength limits | All 15 documented strengths, 0/1/t faults per strength | Corrected main/user data and error counts matched for the selected patterns |
| Metadata/parity faults | 53 additional targeted main/user/parity cases | All corrected; includes protected-user lengths 4 through 32 bytes |
| User-length encoding | 0 through 32 bytes in steps of 4 | Isolated codeword readback matched the selected length |
| Stale context | 8 pages, PIO/MDMA writes and three read paths after poisoning all four length words | Unused words cleared; data, user metadata and tail preserved |
| Public MTD | PLACE/AUTO/RAW, data-only and OOB-only; 35 reads after six supported writes | Requested bytes and buffer bounds matched; 1024-byte partial write rejected with `EINVAL` |
| Installed subpage callback | Five page patterns × six ranges, compared with full-page and public MTD reads | All requested-byte comparisons and expected ECC error results matched |
| Load/guards | 2,000 alternating PIO/MDMA reads under three 16 MiB RAM fill/verify workers | No data, guard, ECC or fallback failures |
| Delayed IRQ delivery | 24 completed transfers with latched CMD/DMA events delivered late | Real IRQ handler acknowledged the events and disabled their mask; subsequent reads matched |
| Completion timeout recheck | PIO, MDMA and installed read callback with IRQ delivery withheld | Real wait-for-completion expired; status recheck accepted completed transfers; all data matched, no fallback |
| Active abort/reset | Initial 24 aborts plus retained-buffer and reset-variant follow-ups | **First subsequent read can be wrong; see below** |

The BCH strengths are 16, 24, 28, 32, 40, 44, 48, 52, 56, 60, 64, 68, 72,
76 and 80. The high-strength cases use **isolated 1 KiB codewords** with spare
bytes placed within the available physical page. They do not claim that all
those strengths form valid full-page layouts on this 128-byte-OOB chip.
Three selected over-limit BCH-32 patterns (33, 40 and 65 faults) reported an
uncorrectable codeword. That does not prove detection of every over-limit
pattern; BCH miscorrection remains possible outside its correction guarantee.
Non-randomized all-zero/all-one codewords also exercised pattern classification.

All 64 seeds selected by the driver's `page % pages_per_eraseblock` rule on
this chip were tested. This does not cover selection of the second half of
the 128-entry seed table on devices with more pages per eraseblock.

## Randomizer transitions

The four-command sequence enables randomization, disables it for command two,
re-enables it for command three, and leaves it enabled for command four.
In NORMAL read commands with page mode set, the second command transfers plain
bytes **without advancing the saved keystream position**. Command three resumes
where command one stopped, even though the NAND column advanced. Rewriting the
same seed/control value while enabling it did not rewind this saved phase.

In block mode each enabled NORMAL read restarts its keystream. NORMAL writes
restart per command in both tested modes. For non-word-sized NORMAL reads the
last byte is not XORed, but it still counts toward page-mode phase advancement.
These are measured H616 behaviors, not universal claims about the older NFCs.

`NandMeasuredToggle` checks symbolic phase/enable transitions; the independent
bit-level replay checks the actual captured bytes. Mutations that advance
through disabled transfers, reset the phase on enable, or randomize the observed
plain tail must fail. This complements, rather than replaces, the prior stream
and full-page ECC models.

## Direct-helper diagnostics versus public callers

The first caller matrix deliberately invoked generic subpage helpers and a
generic subpage writer directly, including a page with only its second step
programmed. Seven requested-range comparisons disagreed with a full-page read
of that artificial page. That is **not evidence of a reachable Allwinner
subpage-read bug**:

- This layout installs `sunxi_nfc_allwinner_oob_read_subpage()`, which performs
  the page-wide read/classification. It does not install either generic helper
  used in that initial diagnostic.
- The public write path rejects the partial-page write used to create that
  artificial case (`NAND_NO_SUBPAGE_WRITE`).
- The installed callback and public MEMREAD follow-up agree with full-page
  results for valid, erased, all-zero, erased-first-step and erased-second-step
  patterns. The last two follow the layout's existing whole-page erase policy.
- A subpage callback receives a full-page buffer; changes to other bytes within
  that buffer are not an out-of-bounds write. Guards were outside the complete
  allocation region owned by the transfer, on separate cachelines.

The original MTD diagnostic also compared OOB sentinels when no OOB was
requested, and did not independently compare AUTO/RAW OOB contents. Its original
booleans are retained for auditability. The separate `mtdoob` follow-up records
the returned synthetic OOB bytes and validates them properly. PLACE comparisons
exclude regenerated parity; AUTO and RAW compare all requested OOB bytes.

## Unresolved first-read-after-abort finding

Follow-up: the [distinct-pattern reset investigation](POST-ABORT-DATAPATH.md)
narrows this to controller data-path state, reproduces raw SRAM displacement,
and compares AHB bus reset. The original observations below are retained
unchanged; that follow-up is a separate capture.

The read-side recovery probe launches a valid two-step MDMA read into a retained
coherent buffer, asserts reset while the controller may still be active, waits
for reset-bit clearance, restores configuration, and issues NAND RESET before
the next driver read. It holds the NAND/controller locks throughout the
sequence and retains its DMA allocation during later observation commands.

The initial matrix had 24 ordinary completed transfers, 24 aborts and 24 delayed
IRQ transfers, with delays from 0 through 5000 microseconds. Fifteen of the
abort snapshots were still active. Eight first post-abort reads disagreed;
two did not increment the failed-ECC count. Completed-transfer controls did
not show the anomaly.

A further 24 aborts alternated which reader came first. Seven first reads were
wrong, including PIO reads. Both following reads matched. The aborted DMA
buffer remained unchanged across all later reads; its surrounding guards were
intact. The observed discrepancy was in the first 1 KiB step, not the second.
Some failures returned a success status and no failed-ECC increment, so merely
retrying reported ECC failures would not address all the observed cases.

An additional interleaved reset-sequence comparison produced:

| Sequence, 24 attempts each | Incorrect first reads |
| --- | ---: |
| Clear ECC, write reset bit alone | 5 |
| Clear ECC, preserve control fields while asserting reset | 12 |
| Preserve control fields, clear ECC after reset | 12 |
| Reset again after the 100 ms observation interval | 4 |

These are diagnostic counts, **not comparative failure-rate estimates**:
first-reader selection and precise abort timing were not fully balanced.
Every variant exhibited the anomaly; none is established as a remedy.
The [pinned BSP `ndfc_soft_reset()`](https://github.com/radxa/allwinner-bsp/blob/2045a3ca2a01f088c0314dc924bda59d154e363e/modules/nand/common1/phy-nand/rawnand/controller/ndfc_v1px.c)
preserves the control fields, but that alone did not resolve it here.

All reset bits cleared; retained buffers were stable at 20 and 100 ms. Those
observations do **not** prove that reset fully clears every sequencer/ECC state
or that an untested hung engine can never access released memory. They also do
not distinguish a probe-protocol defect from a controller recovery requirement.
No production-kernel fix is inferred from these results yet. No ordinary
non-injected page read failed in the stress or functional matrices.

`NandResetObservation` explicitly separates a quiet retained buffer from correct
subsequent data. It rejects freeing the probe buffer based only on the quiet
interval and rejects a claim that quietness proves the next read correct. The
second-read success is a **bounded observed trace**, not an assumed universal
recovery guarantee or a proposal to discard the first read in production.

## Validation and remaining limits

`python3 run_hardware.py` replays 5,954 extended observations as well as the
earlier capture. It recomputes physical encodings and stream digests, checks
PIO/MDMA counters and guards, and explicitly retains the reset finding and
initial diagnostic mismatches. Seven host-only safety tests exercise backup
validation, Python assertion requirements, and reversible bridge installation.
The two added TLA+ models have 20 positive/negative expectations. Passing these
checks validates the stated finite models and capture interpretation, not all
possible silicon behavior.

The complete regression run passed **65/65 jobs**, including **1,196 expected
TLC outcomes** across the ordinary models and trace-refinement runners. The
candidate source hash matched the digest above and the runner verified that
its executable inputs did not change during the run. Deliberate mutations,
known baseline defects and weakened-assumption counterexamples count as passing
expectations only when their specific expected property fails.

The general technical-patterns and false-positive guides, plus locking,
I/O-accessor and alignment prompts, were checked from Sashiko commit
`feed8a17375278783d7277a5f1eedc32fa3230f9`. There is no dedicated NAND prompt
in that checkout. This supplements the source, hardware and model checks; it
does not imply bot approval. The reset finding remains unresolved.

Still outside this board's coverage: real larger-page/larger-OOB media,
multiple chips and chip selects, older controller generations, external DMA
providers, DDR interfaces, a genuinely stuck DMA engine or failed reset,
voltage/temperature corners, power loss during NAND programming, long-term
retention/read-disturb and destructive lifetime/unbind races. The full driver
and proximal-code source models retain their explicit assumptions for those
areas. This work does not claim a complete model of every NFC generation.
