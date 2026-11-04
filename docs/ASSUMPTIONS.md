# Assumptions, findings and limits

## Results and their classification

The positive model configurations and host checks pass under their stated
contracts. The tests also reproduce source-level observations:
split address-cycle loss, a failed clock change cached as successful, ignored
external DMA configuration failure, read-retry OOB misplacement, and ignored
BBT scan/read transport errors, ignored timing-recovery failures, open-reference
cleanup, upper DMA IRQ-bank dispatch and stale cyclic callbacks. Their actual-source
reproducers and reachability limits are in [FINDINGS.md](FINDINGS.md).
The hardware audit adds a direct-buffer transfer-count mismatch affecting
large extra-OOB tails. Earlier tests conflated SRAM capacity and transfer
count; their above-1024-byte success results are superseded by the corrected
[manual-based fixtures](HARDWARE-CONTRACTS.md).
The updated v10 fixes that mismatch separately; pinned v9 still demonstrates
it. H616 counter width, older-controller valid requests and physical RAM-bank
bounds are distinct contracts. The A10 manual lacks an NFC register chapter
and the A23 manual was unavailable, so older details use pinned BSP evidence
or explicitly explored alternatives, not unsupported H616 extrapolation.
The provider audit establishes that the injected configuration error is not
reachable through the pinned sun4i/sun6i config callbacks, which always return
zero. Other findings are not dismissed merely because they require failures.
The probe audit adds unavailable DT-child scanning and live first-chip I/O
racing with the next child's identification. Both reproduce in the pinned
source and pre-fix candidate. The integrated v10 now fixes both;
[PROBE-VISIBILITY.md](PROBE-VISIBILITY.md) records the source paths and fixture
boundaries, and [probe trace conformance](PROBE-TRACE-CONFORMANCE.md) checks
observations from the fixed driver's probe and unwind against TLA+.

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

- Normal MTD operations on the shared controller are serialized by the NAND
  core. Page-transfer models allow IRQ interleavings, not two unrelated page
  operations concurrently on one controller. This is **not** a guarantee for
  probe: `nand_scan_ident()` does not acquire that mutex. The separate
  `NandProbePublication` model and source tests reproduce scan/I/O interference
  after the first child is published. Deferred publication is now implemented
  in the v10 candidate, with separate source and trace-conformance checks.
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
- `NandLifecycle`'s removal safety is conditional on excluding users before
  resource release. The new `NandMtdLifetime` and actual MTD/partition-removal
  tests establish that the pinned unregister path does **not** itself wait
  for open users: registration references and NAND resources have different
  lifetimes. The assumption cannot be cited as an actual core guarantee.
  Full device-core/sysfs/devres concurrency is still outside the host fixtures.
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
compile core locking or device-core removal. The lifecycle runner now compiles
scan-identification orchestration, scan-tail, cleanup, timing negotiation and
the actual `nand_get_device()`/`nand_release_device()` lock sequence (with
host mutex services, not real threads);
the new discovery-boundary runner executes actual ONFI/ID decoders into the
actual sunxi attach functions. Manufacturer internals and real transport remain
fixtures. The new composed runner connects core requests and BBT traffic to
driver page/OOB/randomizer bodies, with raw storage and command transport
still explicitly emulated.

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
  generic policy enforced by sunxi. Software BCH encoding/decoding is now checked
  against an independent polynomial encoder and composed with driver helpers.
  Hardware BCH behavior, analog cell behavior, wear,
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
  PIO/DMA paths against a bit-level oracle. The original matrix uses synthetic
  parity; the codec extension uses real software BCH parity and correction.
  Neither is an end-to-end hardware emulator or a captured-media golden test.
- A raw reread is allowed to disturb controller scratch/user-data state in the
  fixtures. This adversarial assumption checks the two-pass capture contract;
  it is not a claim that every controller overwrites every scratch register.
- The models do not establish the vendor format's compatibility with every
  bootloader version or NAND part. That still requires authoritative vendor
  information and actual media/hardware tests.

`NandOobPipeline` adds byte-exact source-observation checks across metadata
capture, ECC accumulation, re-encoded spare classification and raw reads.
Its status and decoded-byte inputs are explicit fixtures, not an emulated
hardware decoder. The caller loop invokes batch/finish helpers rather than
the entire page entrypoint. Its synthetic parity inputs complement, but do
not replace, the real software BCH checks. See
[OOB-PIPELINE-CONFORMANCE.md](OOB-PIPELINE-CONFORMANCE.md) for the finite
matrix, negative controls and still-unmeasured hardware boundaries.

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
[INTEGRATION-COVERAGE.md](INTEGRATION-COVERAGE.md). The deeper six-area extension
is in [DEEP-VALIDATION.md](DEEP-VALIDATION.md); the subsequent boundary and
historical-regression work is in [BOUNDARY-VALIDATION.md](BOUNDARY-VALIDATION.md).
Command, manufacturer retry, lifetime-policy and byte/cache-line refinements
are mapped in [REFINEMENT-VALIDATION.md](REFINEMENT-VALIDATION.md).
Cross-command randomizer phase, DMA aperture contracts and persistent packed
lengths are mapped in [STREAM-APERTURE-CONTEXT.md](STREAM-APERTURE-CONTEXT.md).
Their counterexamples distinguish deliberately broken code from violations
of nonzero-size, mapping-mask, coherent-allocation and hardware-reload
assumptions. In particular, an injected above-mask DMA address is not evidence
that the actual DMA API can return one; a successful randomized round trip is
not proof of compatibility with an independently encoded flash image.
DT availability and scan/publication concurrency are mapped separately in
[PROBE-VISIBILITY.md](PROBE-VISIBILITY.md). Its positive admission and deferred
publication policies have matching v10 candidate fixes; they are not claims
that the pinned reference is fixed. OF node references,
dynamic overlays and complete registration/unbind concurrency remain outside
those bounded models.
All distinguish modeled from
source-executed paths. Remaining end-to-end gaps include device-core probe/unbind,
every combination of allocation failures, concurrent provider IRQ/tasklet
interleavings and IOMMU behavior,
physical reset/clock/pinctrl, complete NAND discovery and optional BBT formats,
firmware/device-tree parsing, and real hardware memory ordering. The driver
supplies no PM callbacks whose suspend/resume protocol could be validated here.
No new hardware testing was performed by these validation runs.

`NandProbeTrace` checks only the recorded scan/list/MTD boundary projections,
not an arbitrary C execution or all concurrent kernel schedules. ASan/UBSan
and GCOV must produce identical traces; all input profiles and single-fault
indices are accounted for. Duplicate projections are checked once with their
multiplicity recorded. Snapshot equality is independent of the model's set
updates, but MTD publication remains a fixture service and retained users are
excluded from the cleanup matrix. This extension does not establish a general
C-to-TLA refinement proof or solve resource lifetime during unbind.

`NandRt` checks safety and eventual progress separately. Removing eventual IRQ
thread scheduling must produce a termination counterexample even though the
drain-before-reuse safety rule still holds. No bounded RT latency is claimed.

The BBT persistence model assumes torn payloads are rejected when `DetectTorn`
is true. Turning that off shows why a recognizable table header is insufficient
to prove an interrupted write preserved its entries. This is not a new checksum
or atomic-commit mechanism implemented by the driver or core.

`NandRecovery` makes outstanding bus writes explicit. Clearing a DMA enable bit
and draining the software IRQ do not, by themselves, prove those writes have
retired. `run_providers.py` checks actual stop/config entrypoint control flow,
but its list and MMIO fixtures do not prove provider concurrency or electrical
quiescence. New late-event, active-command and pending-write counterexamples
record the distinct guarantees still needed from hardware.

`run_provider_boundary.py` now executes provider IRQ/dispatch/tasklet and
virt-dma list/callback bodies, including no-client-callback operation matching
the NAND client. It does not prove bus quiescence. The generic model's
callback-access-after-unmap witness applies only when a client callback uses
that mapping; it is not directly applicable to sunxi NAND, which installs none.
The separate sun4i cyclic witness is outside NAND's transfer modes. The
provider harness's single function-local UBSan null-member-address exclusion
is documented in the boundary map; it must not be hidden as an unqualified
all-sanitizers pass.

`run_transport.py` executes the real Hynix/Micron retry command sequences, but
the emulator explicitly chooses whether a command took effect before its
reported timeout. Staged/active parameter retention is a chip-state contract,
not a measured property of every supported NAND. Source error propagation
does not imply hardware rollback. Default-restoration failures are checked
without pretending that a successful read's data was reported as success.

`NandDetachPolicy` and the recovery model's quarantine policy are alternative
ownership designs, not code present in the sunxi driver. Their passing cases
do not resolve the retained-user/unbind finding. `NandDmaBytes` similarly
states the cache-line ownership contract; it does not replace testing an
architecture's actual DMA/cache-maintenance implementation.
