# NAND-core, media and recovery integration coverage

This extension covers the six requested follow-up areas. It adds seven models
and a third C harness without changing the pinned kernel implementation.
Coverage means the listed finite cases and source paths were exercised; it does
not mean all silicon behavior or all NAND-core executions have been proved.

## Coverage map

| Area | Model coverage | Source-executed checks and boundaries |
| --- | --- | --- |
| BBM / BBT lifecycle | `NandBbt`: four blocks, factory markers, scan failure, mirrored creation/update, runtime bad block, remount, optional omission of runtime OOB markers | Actual BBM page selection/marking, low-level markbad and BBT entry points; first/second/last-page combinations and 8/16-bit markers; `PLACE_OOB` scanning despite ECC errors; in-band/OOB tables, per-target tables, creation, mirror selection, relocation, reserved regions, no-space and read errors |
| MTD requests and read retry | `NandRequest`: three distinct pages, raw/ECC reads, OOB copy ordering, successful retry, exhausted retry, transport-error prefixes and statistics | Actual `nand_do_read_ops`, `nand_do_read_oob`, write counterparts and OOB layout helpers; `AUTO_OOB`/`PLACE_OOB`/`RAW`, fragmented free regions, nonzero OOB offsets, bounce buffers, partial requests, two targets, retlen/oobretlen, cache/subpage dispatch and failed setup-retry |
| Stored-format and randomizer composition | `NandFormat`: two pages of three steps; independent writer/reader batches of 1/2/3, clamped at page boundaries; seed reuse and incompatible-reader rejection | 168 stored-byte combinations using actual batching, user-length packing, protected-byte accessors, BBM transform, randomizer and vendor unprotected-OOB reader; registers discarded between write/read; independent bit-level LFSR oracle, distinct payload/parity/tail and seed-table boundary pages |
| IRQ/timeout/RT scheduling | `NandController` now separates status read, mask read, status acknowledge and mask write; `NandRt` separately permits the IRQ thread to starve while a caller waits for drain | Actual IRQ/wait/reset helpers are executed by `run_lowlevel.py`; the composed model checks two operations/two batches, partial events, timeout/drain/recheck and reuse. Scheduling is modeled, not implemented by host threads or a PREEMPT_RT kernel |
| Backend/register and DMA ownership | `NandOwnership`: coherent early publication is possible but not guaranteed; barriers order descriptors, map/unmap transfer buffer ownership, reset/termination gates unmap | Actual DMA prepare/cleanup/init use capability expressions extracted from A10/A23/H616 data. Checks restrict MMIO writes to common and backend-specific registers, preserve external-DMA control bits, verify packed descriptors, alignment, directions, status acknowledgement and publication order |
| Erase/program/persistence | `NandPersistence`: two BBT copies, cuts before/during/after erase/program, detectable versus unrecognized torn payloads, versions 0/127/254/255. `NandProgram`: two pages, two requests per erase epoch, two epochs, finite device budgets 1/2/4 | Actual BBT erase/write/update/search/read path uses an AND-programming media fixture. Tests interrupt the update between primary and mirror, including version rollover. Actual raw driver C also checks one-to-zero programming and partial failure. A part's program budget is an explicit caller/device contract, not a new driver check |

## Source fidelity

`source.json` pins six additional source/header files from the same sent commit
as the driver. Their contents were compared byte-for-byte with that commit.
`run_core.py` extracts **46 complete function bodies unchanged**, records their
hashes and source line numbers, and compiles them with ASan/UBSan. A separate
GCOV build excludes fixture code and requires every extracted function to run.
The exact list is in `run_core.py` and each generated summary.

The core fixture has two targets, 64 blocks per target, four 1-KiB pages per
block, 64-byte OOB, two-bit BBT entries and a deliberately fragmented six-byte
`AUTO_OOB` layout. This small geometry makes full scans practical. The driver
layout tests separately cover actual sunxi ECC/OOB geometries; the core fixture
is not presented as an H616 NAND part or as the hardware BCH configuration.

Kernel API boundaries still exist. NAND page/ECC callbacks, transport failures,
erase/program results, device locks, allocation, continuous-read disablement
and DMA/provider behavior are host fixtures. Full `nand_scan()`, identification,
ONFI negotiation, user-copy/ioctl plumbing, device removal and scheduler internals
are not executed. BBT tests use two-bit, versioned tables; other table bit widths
and every optional descriptor mode are not exhaustively covered.

## Format interpretation

The three C formats are generic unrandomized, randomized with a plain physical
BBM, and Allwinner vendor format. Each uses its own matching reader. The tests
cover PIO→PIO, PIO→DMA, DMA→PIO and DMA→DMA, legacy/fixed versus variable user
data, and pages 0/1/63/64/127/128/255. They are **not** an assertion that different
formats, arbitrary BCH strengths, or every bootloader version are compatible.

Main data and protected metadata round-trip byte-for-byte. Vendor unprotected
parity is checked in the deliberately different PIO-decoded/DMA-raw
representations; the tail decodes to the same original bytes. ECC parity in the
fixture is synthetic and distinct per byte/step. The suite does not implement
or validate a complete physical BCH encoder/decoder. The per-step seed reload
and hardware ECC stream boundary remain controller contracts.

`ReaderCompatible` in the symbolic model is an explicit format agreement,
not a compatibility probe implemented by Linux. `FormatBoundary` makes this
limitation visible instead of treating an arbitrary reader as valid.

## Persistence and scheduling limits

Successful mirrored BBT updates retain old entries and the newly acknowledged
entry across the modeled interruption points. A newer, valid primary is selected
over an older mirror even across 255→0. The C tests execute the real subsequent
repair path; the symbolic model abstracts selection and does not reproduce
every version-repair assignment.

A torn table with a recognizable header and undetected bad payload can lose
entries. `integrated-persistence-assumption-undetected-tear` demonstrates this
dependency; it does not invent a commit bit/checksum absent from the Linux format
or claim that such a tear occurred on the test device. Likewise, power loss
before a markbad update is acknowledged need not preserve the new bad-block
entry when OOB markers are disabled. Old committed entries must survive.

The media budget model permits only one-to-zero programming and requires erase
before restoring ones. Its positive cases assume the caller respects the NAND
part's partial-program budget. Removing that assumption fails `ProgramBudget`;
no generic sunxi mechanism enforcing a per-part budget is claimed. Wear,
retention, page-pair ordering, analog erase/program failure and arbitrary
power-collapse cell behavior require device-specific evidence.

The positive RT case assumes the pending IRQ thread eventually runs.
The negative case drops that assumption and must fail **`Terminates`**, not a
convenient safety invariant. IRQ draining prevents reuse while the old handler
is active but cannot impose a real-time bound on a permanently starved thread.
This is an explicit scheduling limitation, not a demonstrated PREEMPT_RT bug.

## Source-level counterexamples

The extension reproduces three additional core issues in unmodified source:

1. A successful read retry can duplicate a page's OOB into the next output slot.
2. BBT creation can ignore a marker-scan transport error and publish an incomplete
   table.
3. BBT readback can ignore a transport error without recovering entries from an
   intact mirror.

See [FINDINGS.md](FINDINGS.md) for paths, conditions and reachability. These are
not attributed to the submitted sunxi patches. Expected counterexample tests
pass only when they reproduce the specified failure; passing does not fix it.

## Review-driven checks

This scope follows the earlier
[BBT marker review](https://lore.kernel.org/r/87ik4fu4fh.fsf@bootlin.com/),
[IRQ timeout review](https://lore.kernel.org/r/20261015071943.7OihPadq@linutronix.de/),
and historical
[external-DMA register discussion](https://lore.kernel.org/r/20201028153545.559ba2be@collabora.com/).
Those reviews guide what to test, not what the tests are allowed to assume.

The applicable Sashiko guidance checked was `review-core.md`,
`technical-patterns.md`, `false-positive-guide.md`, the subsystem index, and
`subsystem/locking.md`, `subsystem/io-accessors.md`, `subsystem/alignment.md`
and `subsystem/mm-alloc.md` from the
[public prompts](https://github.com/sashiko-dev/sashiko/tree/main/third_party/prompts).
There was no NAND-specific prompt in the inspected set. Caller/error-path
verification and actual-source witnesses distinguish findings from abstract
hardware assumptions. This is not a Sashiko bot approval or a kernel build.
