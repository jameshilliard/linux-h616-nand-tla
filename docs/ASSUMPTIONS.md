# Assumptions, findings and limits

## No confirmed new driver defect from this validation

The positive model configurations and host checks pass for the pinned series.
Deliberately broken variants demonstrate that the named checks can fail. They
are regression sensitivity tests, not claims that those broken variants are in
the submitted source, nor that each is a byte-for-byte historical revision.

Two issues encountered while developing the suite were validation issues:

- The first ECC model retained a previous batch's capture bookkeeping while
  advancing the logical batch base. The bookkeeping was corrected; no driver
  change was made.
- An initial C assertion assumed that successful controller ECC initialization
  always implied at least one hardware step. On legacy 512-byte pages with a
  requested 1 KiB step, `nand_scan_tail()` switches to software ECC before page
  callbacks run. The test now classifies these as core-fallback cases. The
  vendor format rejects zero steps instead.

## DMA stop is a real prerequisite, not a proven hardware property

`sunxi_nfc_dma_op_abort()` calls controller reset for integrated DMA or
`dmaengine_terminate_all()` for an external channel. Their return values are not
propagated by that helper. Cleanup subsequently unmaps the buffer.

The positive transfer model assumes abort has stopped further DMA accesses.
`assumption-abort-does-not-stop-dma` removes that assumption and reaches
`NoUnsafeUnmap`: a timeout followed by ineffective abort permits unmapping while
DMA is still active. The C fixtures make the same successful-stop assumption.

This is an explicit unresolved hardware/provider contract. The witness does
not establish that a supported controller continues DMA after a reported reset
failure, or that the series creates a new regression. Establishing reachability
requires the applicable reset/termination implementation, controller behavior,
and fault-injection evidence. The harness deliberately does not turn this into
an automatic kernel fix.

## Controller and core contracts

- Operations on the shared controller are serialized by the NAND core. The
  model allows IRQ interleavings, not two unrelated page operations executing
  concurrently on one controller.
- The previous hardware command must be quiescent before another begins. The
  IRQ model covers an already executing handler and stale software state, not
  arbitrarily late hardware events from a command that never stopped.
- `disable_irq()` is modeled as blocking new handler entries and waiting for
  the running handler. MMIO writes/readback have their documented ordering.
  Weakly ordered buses, posted-write timing and cache coherency are not modeled.
- A later hardware slot in a PAGE_OP uses page-relative main-data columns.
  After a manual nonzero column change, the driver therefore limits later
  variable-length batches to one slot and restores the spare base. This is an
  input hardware contract, not something TLC discovers about silicon.
- Source initialization admits page/step layouts before transfer models run.
  The C matrix directly checks its limits. MTD callers provide valid page and
  subpage ranges; arbitrary malicious kernel call arguments are not enumerated.
- The core interprets failed ECC counter deltas. A driver callback returning
  zero after classifying an all-zero vendor page does not mean that the page is
  valid: that path increments the failure count by the number of steps.

Relevant core contracts were checked in the source accompanying the pinned
submission: `nand_get_device()`, `nand_read_page_op()`,
`nand_change_read_column_op()`, `nand_prog_page_begin_op()`,
`nand_prog_page_end_op()`, and `nand_scan_tail()`. The host tests do not compile
the NAND core itself.

## ECC, media and representation

- ECC results and corrected bytes are inputs to the model. BCH polynomial
  decoding, analog error rates, NAND program/erase behavior, partial-program
  limits, power failure and wear are outside scope.
- The Allwinner erased-page check is a firmware compatibility heuristic over
  re-encoded, ECC-corrected protected bytes. Passing it is not mathematical
  proof that the physical page was never programmed. The zero-page rule has
  precedence, and failure paths preserve decoded main/protected bytes for BBT
  consumers.
- PIO and DMA intentionally expose different normal parity-byte representations;
  both use the page seed for the unprotected tail. The model uses representation
  labels, while the C harness checks actual byte transforms independently.
- A raw reread is allowed to disturb controller scratch/user-data state in the
  fixtures. This adversarial assumption checks the two-pass capture contract;
  it is not a claim that every controller overwrites every scratch register.
- The models do not establish the vendor format's compatibility with every
  bootloader version or NAND part. That still requires authoritative vendor
  information and actual media/hardware tests.

## Deliberate abstraction boundaries

The models are hand-written, with function-level correspondence documented in
`COVERAGE.md`; they are not generated proofs of the C implementation. The
transfer model normalizes PIO/DMA cleanup into a per-batch state machine. Actual
PIO holds ECC enabled across the page loop, which is checked by the extracted C
control-flow tests. Counter values are bounded examples, not a proof against
unbounded cumulative-statistics overflow. Liveness uses weak fairness and makes
no real-time latency guarantee.

Not covered end-to-end: allocation failure at every kernel allocation, DMA
descriptor submission internals / IOMMU mapping, command-parser decomposition,
clock/reset/pin configuration, NAND timings, PM/unbind/probe lifecycle, other
MTD users, raw/BBT scanning algorithms, firmware/device-tree parsing, or memory
barriers on physical hardware. No new hardware testing was performed for this
repository's initial validation. These limits should remain visible as coverage
is extended.
