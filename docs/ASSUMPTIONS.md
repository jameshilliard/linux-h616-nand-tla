# Assumptions, findings and limits

## Results and their classification

The positive model configurations and host checks pass under their stated
contracts. The tests also reproduce six conditional source-level failures:
split address-cycle loss, a failed clock change cached as successful, ignored
external DMA configuration failure, read-retry OOB misplacement, and ignored
BBT scan/read transport errors. Their actual-source
reproducers and reachability limits are in [FINDINGS.md](FINDINGS.md).

Deliberately broken variants demonstrate that the named checks can fail. They
are regression sensitivity tests, not claims that those broken variants are in
the submitted source, nor that each is a byte-for-byte historical revision.

Two issues encountered while developing the initial suite were validation issues:

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

The positive transfer and composed-controller models assume abort has stopped
further DMA accesses.
`assumption-abort-does-not-stop-dma` removes that assumption and reaches
`NoUnsafeUnmap`: a timeout followed by ineffective abort permits unmapping while
DMA is still active. The page-control-flow C fixture assumes successful stop;
the low-level fixture executes the actual abort/reset helpers and separately
injects ineffective reset/termination to expose the dependency.

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
- The previous hardware command must be quiescent before another begins.
  `NandController` has negative tests for a PIO timeout with a still-active
  command and an old command event arriving after reuse. PIO has no explicit
  abort/reset in the submitted cleanup path; its model cleanup must not be
  mistaken for an actual reset call.
- External DMA waits for the controller command event rather than an independent
  DMA event. A negative model permits that event before DMA has drained and
  reaches unsafe unmapping. The positive case assumes the required ordering.
- `disable_irq()` is modeled as blocking new handler entries and waiting for
  the running handler. MMIO writes/readback have their documented ordering.
  Descriptor publication separately models stores, visibility and `dma_wmb()`;
  `NandOwnership` adds streaming-map/unmap visibility and nondeterministic early
  visibility for coherent descriptors. This is not a complete weak-memory,
  posted-write, Linux memory-model or cache-coherency model.
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
- Removal relies on MTD/NAND core excluding new users and draining existing
  operations before release. `NandLifecycle` makes this contract explicit and
  demonstrates the failure when it is removed. Kernel unregister/devres
  implementations are not compiled into the host fixtures.
- Clock changes must succeed and rounding must preserve computed timing
  constraints. Both have negative model tests; failed rate-setting also has
  an actual-source reproducer. This suite does not prove a clock provider's
  hardware behavior or all possible rate-tree changes.

Relevant core contracts were checked in the source accompanying the pinned
submission: `nand_get_device()`, `nand_read_page_op()`,
`nand_change_read_column_op()`, `nand_prog_page_begin_op()`,
`nand_prog_page_end_op()`, and `nand_scan_tail()`. The low-level harness compiles
the unchanged operation parser and suboperation helpers from that same core
revision. The separate integration harness now compiles selected unchanged
core request/read-retry and BBT scan/create/update/read functions. It does not
compile the complete scan/identification flow, core locking or device removal.

The contracts are consistent with the
[DMAengine client interface](https://docs.kernel.org/driver-api/dmaengine/client.html),
[DMA API](https://docs.kernel.org/core-api/dma-api-howto.html), and
[common clock framework](https://docs.kernel.org/driver-api/clk.html).
Those APIs do not establish that every provider or controller satisfies the
assumptions in this particular recovery sequence.

## ECC, media and representation

- ECC results and corrected bytes are inputs to the model. Bit-level NAND
  programming and partial-program failures are modeled, and repeated programming
  is tested in C. Erase, finite program-count limits and interrupted BBT updates
  now have explicit models. The limits are device/caller contracts, not a
  generic policy enforced by sunxi. BCH decoding, analog cell behavior, wear,
  retention and a specific part's power-failure guarantees remain outside scope.
- The Allwinner erased-page check is a firmware compatibility heuristic over
  re-encoded, ECC-corrected protected bytes. Passing it is not mathematical
  proof that the physical page was never programmed. The zero-page rule has
  precedence, and failure paths preserve decoded main/protected bytes for BBT
  consumers.
- PIO and DMA intentionally expose different normal parity-byte representations;
  both use the page seed for the unprotected tail. `NandOob` uses representation
  labels; `NandMedia` uses reversible masks. The integration C checks build a
  physical-byte image, discard scratch registers, and read it with matching
  PIO/DMA paths against a bit-level oracle. Parity is synthetic: this is not an
  end-to-end hardware BCH/page emulator or a vendor-media golden-image test.
- A raw reread is allowed to disturb controller scratch/user-data state in the
  fixtures. This adversarial assumption checks the two-pass capture contract;
  it is not a claim that every controller overwrites every scratch register.
- The models do not establish the vendor format's compatibility with every
  bootloader version or NAND part. That still requires authoritative vendor
  information and actual media/hardware tests.

## Deliberate abstraction boundaries

The models are hand-written, with function-level correspondence documented in
`COVERAGE.md`; they are not generated proofs of the C implementation. The
transfer models normalize PIO/DMA cleanup into per-batch state machines. Actual
PIO holds ECC enabled across the page loop, which is checked by extracted C
control-flow tests. Counter values are bounded examples plus a direct C wrap
check, not a proof against arbitrary lifetime-wide statistics errors. Liveness
uses weak fairness and makes no real-time latency guarantee.

The first seven follow-up areas are itemized in
[EXTENDED-COVERAGE.md](EXTENDED-COVERAGE.md); the six integration areas are in
[INTEGRATION-COVERAGE.md](INTEGRATION-COVERAGE.md). Both distinguish modeled from
source-executed paths. Remaining end-to-end gaps include device-core probe/unbind,
every allocation failure, provider internals/IOMMU behavior, physical
reset/clock/pinctrl, complete NAND discovery and optional BBT formats,
firmware/device-tree parsing, and real hardware memory ordering. The driver
supplies no PM callbacks whose suspend/resume protocol could be validated here.
No new hardware testing was performed by these validation runs.

`NandRt` checks safety and eventual progress separately. Removing eventual IRQ
thread scheduling must produce a termination counterexample even though the
drain-before-reuse safety rule still holds. No bounded RT latency is claimed.

The BBT persistence model assumes torn payloads are rejected when `DetectTorn`
is true. Turning that off shows why a recognizable table header is insufficient
to prove an interrupted write preserved its entries. This is not a new checksum
or atomic-commit mechanism implemented by the driver or core.
