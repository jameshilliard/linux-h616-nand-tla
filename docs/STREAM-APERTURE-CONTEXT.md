# Cross-command streams, DMA apertures and persistent register context

This extension checks three contracts that individual function coverage does
not establish. It adds 98 bounded TLC configurations and matching source-body
tests, preserving both the posted v9 reference and the separately identified
[v10 candidate](CURRENT-DRIVER.md). It changes validation code, not the kernel.

| Area | Model and source checks | Still assumed |
| --- | --- | --- |
| Raw randomizer stream | `NandRandomizerStream`; independent physical-byte encoding/decoding; chunk errors, seed changes, plain requests and zero-length calls | Continuous LFSR phase between normal commands; physical seed-loading behavior |
| DMA address aperture | `NandDmaAperture`; mapping endpoints, 32-bit mask, descriptor extent/alignment, actual little-endian fields and list-base writes | DMA API supplies mappings and coherent allocations satisfying their contracts; no IOMMU/provider execution |
| User-data register reuse | `NandUserDataContext`; wide/narrow batches, PIO/DMA transitions, zero lengths, packed-word boundaries and logical-step offsets | Serialized, register-valid inputs; source allocator and batch planner determine which combinations are reachable |

## Independent randomizer stream

The formal model keeps the requested seed and logical byte position separate
from the controller's loaded seed and current stream phase. The reference
encoding does not reuse the modeled writer's result. Each successful read
must decode bytes encoded with the reference key, and each write must store
the reference representation, including any physically written prefix of a
failed command. Failed reads do not copy their partial command buffer.

Thirty-two positive configurations cover read/write, plain/randomized data,
two command limits, and errors in the first three commands or no error. Each
configuration contains subsequent operations with changed seeds, a zero-length
request and plain I/O. Four mutations check unwanted chunk reseeding, stale
seeds, continuing the previous operation's phase and failed enable-bit cleanup.
Two assumption cases allow a hypothetical automatic reload at command start.

The low-level C fixture executes the unchanged randomizer wrappers and raw
buffer helpers. Its new independent oracle uses a bit array, two explicit
page seeds and the 15-clock warm-up. It shares neither the MMIO fixture's
shift routine nor the source helper/table lookup. Existing broader C tests
check all 65,536 input seeds and both derived ECC seed tables.

The current candidate runs 48 read/write, seed, plain/scrambled and partial-error
sequences across A10/A23/H616. They compare every stored or returned byte,
untouched suffixes and enable-bit cleanup, then change seed/page and perform
plain I/O without resetting the controller. The pinned reference runs 32
sequences on A10/A23; its already-known oversized-H616 count witnesses remain
separate instead of being accepted by this positive test.

A second fixture mode deliberately reloads the randomizer at every command.
Writing and reading 2,057 bytes with the same chunk boundaries still produces
a successful round trip, but the independent physical-image comparison fails
starting at the second 1-KiB command. There is one such witness per exercised
controller profile. This proves a limitation of round-trip testing, **not**
that those controllers actually reload. The positive continuous-stream case
and the negative reload alternative remain explicit hardware assumptions.

## DMA address and caller contracts

The model uses two 16-bit limbs to explore addresses around 4 GiB without
overflowing TLC's signed integer representation. It checks the complete
buffer interval, not only the first address, and checks the 16-byte descriptor
allocation separately. Register MDMA and descriptor MDMA use 32-bit fields;
the external-DMA profile passes the mapped address to the provider and makes
no claim about that provider's register format.

There are 33 positive configurations, two mutations and five assumption
counterexamples. Cases include an exact final byte at `0xffffffff`, buffers
crossing that boundary, an address above 4 GiB, descriptor allocations that
fit or cross the boundary, and length/alignment restrictions. A hypothetical
missing explicit mask matters when the inherited mask is wider; that mutation
does not claim the actual OF-created H616 device currently starts that way.

The unchanged source explicitly sets a 32-bit streaming/coherent mask for
descriptor MDMA. The older profiles inherit the device mask: at the pinned
kernel base `30daaf3e7897`, `of_platform_device_create_pdata()` initializes it
to 32 bits and `of_dma_configure_id()` narrows it against the DMA ranges.
Those are software initialization contracts, not hardware documentation.
The [DMA API guide](https://docs.kernel.org/core-api/dma-api-howto.html)
explains the mask and the distinction between CPU and device DMA addresses.

The C fixture adds 52 cases: buffer endpoints in both directions on all three
backends, and four descriptor-allocation cases. It checks mask setup, errors
before register publication, exact descriptor/list/register values, balanced
unmaps and preserved pointer width. Artificially returning an above-mask
mapping produces two expected truncation witnesses in the integrated-DMA
paths. Those injections violate the DMA API contract; they do not establish
a reachable driver defect or justify redundant checks in the driver.

Nonzero request size is a **caller precondition**. In the positive formal
model the mapping boundary rejects zero before the driver transfer actions;
this abstracts admission, not a promise that the actual DMA API safely rejects
every zero-sized request. The negative zero-request case bypasses that
precondition and exposes the descriptor's special zero-length meaning. The
C fixture asserts the precondition. Multi-descriptor zero-length chaining,
IOMMU translation, bounce allocation and physical memory retirement are not
modeled by this aperture extension.

## Persistent packed user-data lengths

The 32 length nibbles start poisoned and persist between requests. DMA writes
each packed word, including zeroes for unused slots. PIO clears the register
bank at the page boundary and then uses slot zero. The model checks the
active encodings and all inactive slots, rather than assuming unused slots
will be ignored by hardware. Legacy profiles must not access length registers.

Sixteen positive configurations cover 1/7/8/9/16/17/31/32-slot boundaries,
zero lengths and logical offsets 0/1/7/8/15/16/31. Four mutations confuse the
logical base, retain stale zero/unused slots or access absent legacy registers.
The source fixture runs 87 persistent sequences and a legacy no-MMIO check.
Adjacent registers are sentinels. The register-valid inputs consume at most the
128-byte user-data bank; logical nonzero starts use one slot, matching the
current candidate's later-batch rule.

This model does not supersede `NandBatch`'s planner or the physical-column
evidence. Admission here means valid encodings, bank bounds and indexed ranges;
it does not claim that the source allocator generates every alternating-length
test table or permits zero bytes for the first logical BBM-bearing step. The
separate actual-allocator and page tests check format admission. Zero-length
PIO fields represent later logical steps, which still use hardware slot zero.
The model does not treat an intermediate partially programmed bank as
ready for consumption: programming is serialized and finishes before launch.

## Remaining work, by value

| Priority | Missing validation | Useful next evidence |
| --- | --- | --- |
| 1 | Hidden randomizer reloads and physical raw-stream compatibility | Independent raw-image vectors across command boundaries, including a separate writer/reader; round trips alone are insufficient |
| 1 | DMA/reset completion versus outstanding bus transactions and late events | Controller/provider guarantees and controlled timeout traces; software IRQ draining is insufficient |
| 1 | Open MTD users during unbind and multi-chip probe unwind | End-to-end kernel reference/devres/locking validation of a concrete lifetime fix; existing source witness remains unresolved |
| 2 | H616 SRAM extent/aliasing and incomplete older NFC register manuals | Non-destructive targeted probes and authoritative older documentation; keep conservative RAM0 bounds meanwhile |
| 2 | C/model correspondence across a whole request | Trace-level refinement of extraction/adapters, including timing and failure ordering; separate passing models and C tests are not a refinement proof |
| 2 | Real concurrency, DMA/cache and MMIO ordering | Kernel fault injection, SMP/RT schedules and architecture-specific ordering evidence, beyond synchronous host fixtures |
| 3 | NAND-part-specific behavior | Captured BCH/randomizer/bootloader compatibility vectors, read-retry effects, program limits and power-loss behavior for identified parts |

The driver has no PM callbacks to invent a suspend/resume protocol for. More
bounded states cannot supply missing silicon guarantees. No new board access,
NAND write/erase or captured private media is part of this extension.

The subsequent [whole-request work](REQUEST-TRACE-CONFORMANCE.md) addresses
the bounded caller-to-DMA/IRQ trace gap and adds compound faults/reuse. The
[file-lifetime work](FD-LIFETIME-CONFORMANCE.md) connects real open/close and
NAND locking, including close/sync after cleanup. These extend the software
evidence; physical contracts and full kernel concurrency remain unresolved.
