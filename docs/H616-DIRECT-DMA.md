# H616 streaming DMA and reset retirement

This is the investigation record, preceding the
[streaming-buffer implementation](H616-STREAMING-IMPLEMENTATION.md). Statements
about the unchanged draft below refer to this experiment's original snapshot.

This investigation separates two questions: whether AHB reset repairs the
[observed post-abort data-path anomaly](POST-ABORT-DATAPATH.md), and whether
the data target needs to be a private coherent allocation. The v10 kernel
candidate is unchanged by this investigation. Its private-buffer policy
remains documented in [the recovery report](H616-ABORT-RECOVERY.md).

## Results

The measurements support streaming-mapped data buffers on this H616 when
AHB reset successfully retires DMA. They do not justify blindly deleting
the private-buffer allocation or treating failed reset as a safe return.

| Recorded matrix | Result |
| --- | --- |
| 128 completed raw transfers followed by reset/reuse | Data, guards and poison checks passed |
| 256 delayed raw abort/reset/reuse trials | All passed; 224 observed the controller busy before reset, split evenly between soft-plus-bus reset and bus-reset-only |
| 8 deliberate writes into the poison buffer | All detected; the checker was not merely reporting cached poison |
| 128 actual-driver direct-DMA abort/PIO-retry trials | All returned expected data and preserved guards; 112 observed busy before reset |
| 152 native control/follow-up reads | Full data/OOB comparisons passed with the appropriate parity representation |
| 400 additional native reads, including 100 repeated 5-us aborts | All passed; no corrected or failed ECC reported |

One of the original 128 native abort trials reported **eight corrected bits**
on the programmed page, returned the expected bytes, and reported no failed
ECC. The other 127 reported no corrections. The 400-read follow-up, including
100 repetitions of that abort timing, did not reproduce it. Its cause is
unresolved: the capture must not be described as uniformly zero-correction,
nor as evidence that this isolated count is a confirmed driver bug.

Three competing RAM workers overlapped part of the raw matrix and completed
2,019, 2,015 and 2,021 verified fill/check passes. This is extra contention
coverage, not proof of worst-case interconnect ordering.

Fresh independent raw dumps of all four SPL copies and the selected UBI
block matched the pre-test backups. The diagnostic module unloaded normally
and an ordinary 32 MiB MTD read completed successfully. No NAND writes were
needed. The board was returned to its normal image; the host build's
diagnostic instrumentation was removed and the ordinary kernel rebuilt.
The [public capture](../hardware/h616-direct-dma-20261104.json) contains
comparison booleans and registers, not original flash contents. Offline
checks cover the exact matrices and deliberately corrupted observations;
they do not re-read the hardware or independently recover private data.

## Experiment

Both phases use a separately booted RAM diagnostic image, detach UBI, hold
the NAND chip and controller locks, and leave diagnostic NAND programming
disabled. They read existing programmed pages; neither phase erases,
programs, reformats or restores flash. Fresh private raw backups cover all
four SPL copies and the additional UBI block used by the second phase.

### Raw descriptor DMA and immediate logical buffer reuse

Operation 28 allocates two cacheable, guarded 4 KiB buffers and a small
coherent descriptor. It maps the data buffers with the streaming DMA API
and requires direct physical mappings, excluding IOMMU/SWIOTLB substitutes.
The first transfer reads 2 KiB of existing NAND main data without ECC or
randomization. Variants either wait for completion or abort after a chosen
delay, using soft-reset-plus-AHB-reset or AHB-reset-only.

Before reset, the probe records controller status, descriptor completion
and the MDMA counter. After reset returns, it records reset state, unmaps
the first buffer and immediately overwrites it with a poison pattern.
There is no additional settling sleep before that ownership transition;
register reads, unmapping and CPU stores still take finite time. The next
transfer reuses the descriptor and reads the same or a different NAND page
into the second buffer. Returned data is compared with an independent raw
pre-test read, not simply with another potentially corrupted DMA read.

The first allocation stays owned by the test. Its *logical reuse* is tested,
not an actual free to an unrelated kernel user. A new bidirectional mapping
must identify the same physical memory. CPU/device synchronization makes
the poison checks observe RAM on this non-coherent ARM64 system. Checks
run immediately after poisoning, after the next transfer, and after roughly
1, 21 and 101 ms. Both guards are part of the streaming mapping. A deliberate
second transfer into the poison buffer is a positive detector control: the
checks must detect that write, rather than always report unchanged memory.

The diagnostic masks IRQ delivery, so NAND setup can reach the driver's
completion-wait timeout and status recheck. This affects experiment duration;
it is not a normal-path performance benchmark. A zero MDMA-counter reading
does not establish that the transfer never started. Active-controller status
is the evidence that some aborts interrupt an unfinished operation; the
test does not identify the exact outstanding bus beats at reset assertion.

### Actual ECC read, streaming mapping, abort and PIO retry

Operation 29 calls the candidate's actual ECC page-read helpers in four
modes: PIO, ordinary private-buffer DMA, direct streaming DMA, and direct
streaming DMA with a one-shot timeout injected after launch. For the direct
modes only, the bridge temporarily clears `nfc->mdma_buf` while holding both
NAND locks, selecting the existing `dma_map_sg()`/`dma_unmap_sg()` path. It
restores the pointer afterwards. The real driver performs reset, cleanup
and whole-page PIO retry; no diagnostic substitute handles those steps.

Controls compare complete main and OOB results on existing ECC-readable
pages using each path's established parity representation. Abort trials
require exactly one DMA preparation, cleanup and PIO
retry. Successful direct reads require no unexpected PIO fallback. The
descriptor must point at the actual caller buffer. Cache-synchronized
post-return checks watch the returned buffer for about 100 ms, followed by
a direct DMA read of a different page. A programmed page with non-uniform
header data and an erased page exercise distinct contents and the erased-page
classification path. ECC statistics are checked. Private flash bytes and
their hashes are not exported.

The native path maps only the caller's data range during the actual read.
Its adjacent guard bytes are initially CPU-cached, so their immediate check
does not independently exclude DMA overruns hidden by dirty cache lines.
The later whole-allocation watch checks stability after its own mapping
transition. The raw experiment, which maps both guards before launch, is
the stronger guard-overrun check; these two observations are not equivalent.

The initial preflight rejected a full PIO/DMA byte comparison. Investigation
localized the difference to exactly 112 BCH-parity bytes, before any abort
injection; direct and private DMA agreed in every byte. This is the existing,
explicitly preserved PIO-decoded/DMA-raw parity representation, not a new
main-data failure. The corrected checker validates DMA parity against the
independent raw backup and the PIO transformation against the bit-level LFSR
oracle. It then requires full-buffer equality against the proper path's
baseline, including parity. The original rejected preflight is retained
privately; parity bytes are not simply excluded from comparison.

The raw phase uses the previously tested v10 diagnostic image, whose source
differs from the final draft by continuation indentation only. The ECC phase
is newly built from the final v10 driver. Both source hashes are recorded
in the capture. The kernel bridge adds counters and timeout injection only;
it is not a proposed production change.

## Hardware documentation and software contract

The [H616 manual](https://www.scs.stanford.edu/~zyedidia/docs/allwinner/h616.pdf)
describes the NAND AHB reset at `NAND_BGR_REG` bit 16, controller soft reset,
and MDMA descriptor/buffer pointer registers cleared by reset. It does not
explicitly specify a universal outstanding-memory-write drain guarantee.
The CCU reset callback writes the reset bit and returns success; a successful
API return is not a bus-level acknowledgement that can independently prove
retirement on physically wedged hardware.

The vendor [NDFC v1px code](https://github.com/radxa/allwinner-bsp/blob/2045a3ca2a01f088c0314dc924bda59d154e363e/modules/nand/common1/phy-nand/rawnand/controller/ndfc_v1px.c)
maps the supplied data buffer directly in `ndfc_dma_config_start()`. This
supports the conclusion that a full-size coherent data buffer is not a
hardware-format requirement. It does not prove the vendor's error paths safe.

The [Linux DMA API](https://docs.kernel.org/core-api/dma-api-howto.html)
requires the transfer to be finished before unmapping/reusing streaming
memory. Unmapping is cache/address bookkeeping, not a controller stop.
Likewise, retaining the descriptor alone cannot protect caller-owned data
memory if hardware can still access it after the failed request returns.

The H616 CCU provider implements exclusive assert/deassert as MMIO writes,
normally returning zero. Its driver suppresses userspace bind/unbind, and
reset-control acquisition holds a provider module reference. Missing or
unusable reset controls must nevertheless be rejected before direct DMA is
enabled; the generic reset API explicitly accepts a NULL optional handle
as a successful no-op. An unexpected failure to assert a real reset remains
a separate lifetime problem, not something a successful-path test resolves.

## Model boundary

The existing `NandPrivateRecovery` abstraction is unchanged. Nine additional
configurations exercise direct caller DMA: eight positive read cases with
successful retirement, including deassertion failure after successful
assertion, plus a missing-reset counterexample. The existing assertion-failure
counterexample remains. All 50 configurations match their expected outcomes.

These are conditional policy checks, not a C-to-TLA+ refinement of the new
diagnostic or proof that the physical reset retires every possible bus write.
The hardware experiment supplies evidence for that contract under the tested
conditions. It does not remove the weaker-contract counterexamples.

## Scope limits

- Deliberate read aborts are not a reproduced permanent hardware or bus hang.
- No NAND program/erase was interrupted. Write-side flash integrity and
  outstanding DMA reads are not established by these read-only measurements.
- Only the available H616 was exercised, not H6, older NFCs or all silicon
  revisions. DMAengine-backed older controllers have different stop paths.
- The 2 KiB page, two 1 KiB ECC steps, available OOB format, tested buffers
  and finite delay/observation windows do not cover every geometry or timing.
- Reuse checks have finite sampling windows and a CPU poisoning interval.
  They cannot prove the absence of every possible late beat.
- No throughput improvement is claimed: the diagnostic observation delays
  and IRQ masking deliberately distort timing.

The small coherent DMA descriptor remains useful even if a production
revision removes the full-size private coherent **data** buffer. Any such
revision must retain the AHB recovery fix and audit mapping ownership,
missing-reset admission, assertion/deassertion errors and final teardown.
Simply deleting the allocation is insufficient: the current failed-abort
path intentionally skips cleanup for its retained private storage. A direct
mapping needs different handling when assertion succeeded but deassertion
failed (memory can be unmapped, but NFC registers must not be accessed),
and cannot be returned to its caller while DMA retirement is unproven.
