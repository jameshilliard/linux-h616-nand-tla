# NAND boundary and historical-regression validation

This extension connects previously separate checks against the pinned OOB v9
source. It does not change the submitted driver or establish correctness for a
different kernel revision. The exact additional inputs are recorded in
`reference/boundaries.json`; the DMA providers retain their existing manifest.

## Coverage map

| Area | Added executable evidence | Remaining boundary |
| --- | --- | --- |
| Core requests → driver → stored bytes | `run_composed.py` runs unchanged core data/OOB request and flash-BBT bodies through unchanged sunxi page, ECC, OOB and randomizer functions. AUTO, PLACE, RAW, bounce-buffer reads, two chip selects, transport-error prefixes and ECC failures are checked. | Separate translation-unit value adapters connect the host structures. Raw storage and command transport are fixtures, not an execution of every `exec_op()`/MTD wrapper. |
| Persistent controller state | The composed fixture retains registers, randomizer state, spare base, column, chip select and stored bytes across operations. `NandSequence` checks mixed ECC/raw/status requests and cleanup after failure. | The three-operation model abstracts formats and timing; the byte-level C test uses a 2-KiB page. The wider geometry/batch matrix remains in the dedicated driver tests. |
| Actual MTD lifetime | Nine unchanged MTD/partition/sunxi cleanup bodies execute with references held across removal, nested partitions, and both partitioned-master configurations. `NandMtdLifetime` separates registration, open references, active I/O and resource ownership. | Device-core, sysfs, module unloading, kobjects and actual concurrent system calls are not executed. A retained reference is deliberately not assumed to exclude administrative unbind. |
| Provider IRQ/tasklet teardown | Twenty-four unchanged sun4i/sun6i/virt-dma bodies execute real list transitions, dispatch, completion, termination and descriptor reuse. Both NULL and non-NULL client callbacks are tested. `NandProvider` separates command completion, outstanding memory traffic and callback/tasklet lifetime; `NandProviderBanks` checks physical-channel indexing. | Sequential event schedules and asserting lock fixtures do not prove SMP exclusion, posted-MMIO completion, or physical bus retirement. Cyclic DMA checks are provider checks, not a NAND transfer mode. |
| ONFI/ID → attach/layout/callbacks | Ten unchanged discovery functions decode ONFI copies, majority repair, extended ECC records, legacy and extended IDs. Decoded geometry/ECC requirements feed the actual sunxi attach and OOB-layout functions. | Manufacturer-specific ID overrides, arbitrary malformed integer combinations and real command transport remain outside this matrix. Scan-tail orchestration is checked separately, not silently assumed to be part of this adapter. |
| Historical regression sensitivity | `run_historical.py` first requires the unmodified generated baseline to pass, then reintroduces four historical bug patterns into temporary generated driver code. Each must fail its specific assertion. | These are semantic mutations adapted to v9, not byte-identical historical kernels. An unrelated crash or sanitizer diagnostic is not accepted as detection. |

## Composed request and persistent-media fixture

Thirty profiles cover legacy/H6-style layouts and plain, scrambled plain-BBM,
and vendor formats. The request matrix includes all PIO/DMA writer-reader pairs;
the BBT matrix creates, updates and reloads distinct primary/mirror copies.
There are 192 explicit request checks and 1,158 page/OOB adapter invocations,
including BBT traffic. Raw invocations use the named storage adapter; the count
is not a claim that every invocation executes a hardware-ECC callback.

The fixture uses two targets with distinct physical pages, stages program data,
and commits by bitwise AND. The controller is not reset between every request.
It checks that ECC/randomizer enable bits and DMA ownership are cleared on
return, while permitting the seed field itself to retain its previous value.
Requiring the entire ECC control register to be zero would be incorrect.

Randomizer setup and byte transforms are actual driver code; simulated
transfers advance state to expose missing per-chunk reseeds. The register seed
evolution is a hardware contract, not a silicon measurement. Parity in this
fixture is an opaque sentinel and ECC results are simulated. The separate
`run_codec.py` suite supplies independent real BCH checks; those must not be
misrepresented as hardware BCH execution within this fixture.

All 112 extracted bodies are included in the composed GCOV report, including
the 23 not called by these scenarios. Broader driver/core tests exercise those
paths separately. This prevents a small connected test from being reported as
full source-branch coverage merely because it compiled all the functions.

## Discovery details

The discovery suite checks 483 scenarios. The ONFI-to-attach submatrix yields
576 accepted and 576 rejected driver profiles across PIO/DMA and legacy/H6
capabilities. Additional checks cover all 256 extended-ID configuration bytes,
ID periods and trailing zeros, and legacy small-page decoding.

ONFI cases use the actual CRC and bitwise-majority functions: valid copies,
first/second-copy corruption, disjoint errors repairable by majority, identical
uncorrectable corruption, and transport failures. Extended-page cases include
invalid signatures, lengths, missing ECC sections, zero codeword size, CRC
failure and transport failure. Three allocation failure points are injected.

`NandDiscovery` is an admission/publication abstraction. Its parity formula is
not a substitute for the C initializer's strength selection and alignment, and
its CRC-copy input does not model majority voting. Those calculations and
repair paths are checked in the source-extracted C suite. The ONFI host
fixtures currently require a little-endian machine and reject other hosts.

## Teardown, provider findings and false-positive checks

The new observations are documented as findings 8–10 in [FINDINGS.md](FINDINGS.md):

- MTD registration references can be dropped without waiting for open users,
  while sunxi immediately runs `nand_cleanup()`.
- The pinned sun6i interrupt loop can complete the wrong physical channel in
  the upper bank, or fail to inspect a partial upper bank.
- The pinned sun4i cyclic termination path can leave a callback pointing at a
  freed contract. NAND uses non-cyclic transfers.

These are source-backed witnesses, not observed board failures or claims that
the OOB series introduced them. They were not checked against every later
upstream/provider patch. No provider or production-kernel fix is applied here.

The NAND client does **not** install a DMA completion callback in
`sunxi_nfc_dma_op_prepare()`. Accordingly, the provider model includes a passing
no-client-callback case without callback synchronization. Its separate
`running-callback` counterexample assumes a client callback that accesses the
mapping; it is not a direct report about the NAND client. Provider tasklet and
descriptor bookkeeping is still tested even when the callback is NULL.

The provider runner has one explicit sanitizer limitation. The unchanged
`vchan_complete()` body forms `&vd->tx_result` with `vd == NULL` for an empty
cyclic callback; the callback helper does not consume that address when no
callback is installed. A declaration attribute disables only that function's
UBSan null-member-address check. ASan and its other UBSan checks remain enabled,
and all other source functions retain the null check. This is not reported as
a demonstrated kernel NULL dereference, nor hidden as an entirely unqualified
sanitizer pass.

The lock/list fixtures retain descriptor storage after logical release so
ownership violations can be asserted without deliberately executing a host
use-after-free. They test specific event orderings, not every concurrent
interleaving inside a provider function.

## Historical fixes and review rationale

Four exact upstream fixes were inspected and their bug patterns adapted:

| Fix commit | Regression injected into the generated v9 body | Required failure |
| --- | --- | --- |
| `4a5a99bc79cdc4be63933653682b0261a67a0c9f` | Remove PIO read-chunk randomizer configuration | Current page seed assertion |
| `44ed1f5ff73e9e115b6f5411744d5a22ea1c855b` | Remove PIO write-chunk randomizer configuration | Current page seed assertion |
| `252173c69eec6998ecf1be7b11d31cd39ce92c9c` | Remove the page-size base from DMA OOB read columns | Parity-region identity assertion |
| `03b1d11a998b70df2ca699a9b9fb8ab5b0746d43` | Omit the hardware subpage-write callback assignment | Attach callback assertion |

Historical review also motivated the boundaries, rather than being treated as
proof of the implementation:

- [Decoded OOB/ECC/BBT review](https://lists.openwall.net/linux-kernel/2026/09/08/2014): preserve representation and failure semantics through real core consumers.
- [AHB DMA completion review](https://lists.infradead.org/pipermail/linux-mtd/2020-October/082826.html): separate controller completion from provider/bus completion.
- [Randomizer read fix](https://lists.openwall.net/linux-kernel/2025/05/19/1105): retain controller state so a missing reseed is observable.
- [OOB-layout review](https://lkml.indiana.edu/2301.0/00710.html): check the actual free/ECC regions and stored bytes, not only nominal capacity.

Applicable [Sashiko guidance](https://github.com/sashiko-dev/sashiko/tree/main/third_party/prompts)
checked for these source observations: general kernel review, technical patterns,
false positives, callstack analysis, locking, I/O accessors, allocation and
alignment. There is no NAND-specific guide in the inspected prompt set.
In particular, caller conditions, configuration branches, absence of NAND
callbacks, channel counts, cleanup ownership and actual error returns were
checked. This is not a claimed Sashiko approval or an exhaustive kernel review.

## Remaining validation work

The six selected areas now have executable checks, but complete kernel/hardware
coverage is not claimed. Remaining gaps include live sysfs unbind against
real MTD operations, SMP tasklet/IRQ races, hardware stop/late-event guarantees,
actual DMA memory ordering, manufacturer discovery quirks, and captured NAND
images validating silicon BCH/randomizer behavior. The host suite does not
access a board or NAND device.

## Independent generic MTD end-of-device OOB fix

Kernel commit `2e91fab3abe62993be81f49f0daebecdf80b8168`, based on
`mtd/fixes` at `1c1a342aceec79528b3ba51f376eca2be5928fe7`, adds a
nonempty-OOB starting-offset check to `mtd_check_oob_ops()`. It does not
change the sunxi submission. The Fixes tag names `d82c36821684`, which
changed the data-range check from `>=` to `>` and admitted a zero-data-length
request at the end of the device.

`run_mtd_boundary.py` can extract the candidate MTD wrappers independently
of the pinned sunxi/NAND sources:

```sh
python3 run_mtd_boundary.py
python3 run_mtd_boundary.py --candidate-mtd-core /path/to/linux/drivers/mtd/mtdcore.c --expect-eof-rejection
python3 run.py --case mtd-
python3 run.py --case NandMtdBoundary
```

The host check covers 1,285,200 bounded admission combinations, including
all three OOB modes, absent buffers, empty requests, last-page access and
invalid offsets. The baseline has 4,165 precisely classified EOF
underflows; the candidate has zero mismatches against the mathematical
contract in that matrix. Source-extracted OOB ioctl helpers also exercise
nested partitions and verify that rejected requests leave the following
page untouched, return zero transferred bytes and never reach the driver.
Data-only fallback on devices without OOB remains valid. ASan/UBSan and
GCOV builds produce identical output, and every newly connected MTD helper
is called. Running the baseline with `--expect-eof-rejection` fails at the
specific EOF admission assertion, as required for the negative control.

The `eof-fixed` TLA+ policy adds only this starting-page guard. It rejects
the EOF OOB witness while retaining the separate data-addition-overflow
counterexample. The `bounded` policy remains a specification alternative,
not a claim that both arithmetic issues have been fixed in the kernel.
The 20 MTD admission/result/counterexample configurations and seven mutation
negative controls match expectations.

The candidate `mtdcore.o` builds with `W=1` for x86-64, and strict checkpatch
reports no issues. An initial ARM64 build attempt was blocked by existing
in-tree generated headers; those files were preserved. No ARM64 build or
hardware reproduction is claimed for this standalone fix.

Review used Sashiko's general review, technical-pattern, callstack,
false-positive, allocation, syscall and Fixes-tag guidance. No dedicated
MTD/NAND prompt exists in the inspected set. Both public wrappers, OOB
ioctl admission and cleanup, partition translation, NULL-buffer length
normalization, no-OOB devices and the original boundary-changing commit
were checked. The remaining data-addition overflow is a separate confirmed
issue, not a regression introduced by this change.

Local evidence: `results/mtd-boundary-eeh6r72i` (baseline),
`results/mtd-boundary-u6e5jd91` (candidate),
`results/mtd-boundary-ii1jurq_` (expected negative-control failure), and
`results/tlc-56hxqw4o` (20 TLC expectations), and `results/tlc-ymk67y2m`
(seven mutation controls). Candidate `mtdcore.c` SHA-256:
`0b0416cc886e3c64799b1d75be73ac3b3ad48ce450745f0b325bc36e11745651`.

## Independent data-range overflow fix

The separate `submit/mtd-oob-bounds-v1` branch contains kernel commit
`c84f69c96780078e22744f6e1242dc820bd9bdde`, based directly on the same
`mtd/fixes` base. It validates the starting offset before comparing the data
length with `mtd->size - offs`. It does not depend on the EOF OOB patch or
the sunxi series. The introducing commit is `5cdd929da53d` (the original
shared OOB sanity checks), rather than the later EOF comparison change.

```sh
python3 run_mtd_boundary.py --candidate-mtd-core /path/to/linux/drivers/mtd/mtdcore.c --expect-data-bounds
python3 run.py --case mtd-
```

The candidate passes ASan/UBSan and source-coverage execution with 784 public
requests and 1,285,340 admission combinations. These include real 64-bit
`SIZE_MAX`/`INT64_MAX` edges, missing-buffer normalization, and read/write
wrapper rejection before a driver callback. The baseline still reproduces
the overflow, and asking it to satisfy `--expect-data-bounds` fails at the
specific arithmetic assertion. These are API-admission tests, not evidence
of a userspace exploit or a live NAND fault. No 32-bit build is claimed.

The model now distinguishes `data-fixed`, `eof-fixed`, and `both-fixed`.
All 35 selected TLC expectations match, including the independent fixes'
remaining counterexamples. The candidate kernel `mtdcore.o` builds with
`W=1` on x86-64; strict checkpatch and b4's local checks pass. b4 exports
one patch message, with no external prerequisite or separate cover message.

Review checked Sashiko's general, callstack, false-positive, allocation,
syscall and build guidance against the actual wrappers and normalization
order. The lifetime work additionally used the locking, sysfs and VFS
guidance; its implementation is still pending.

Evidence: `results/mtd-boundary-b9iggzot` (candidate),
`results/mtd-boundary-r9i31sy9` (baseline),
`results/mtd-boundary-votpb6v3` (expected baseline failure),
`results/tlc-h_gzjs9a` (35 TLC expectations), and
`results/tlc-dnykit5q` (seven mutation controls).

### Partial-page OOB review finding

[Sashiko's review](https://lore.kernel.org/r/sashiko-outbox-197995@kernel.org)
identified another case: a read-only partition may end inside a page.
An offset in that last partial page is below `mtd->size`, but both floor
divisions in the OOB-capacity calculation produce the same page number.
Subtracting a nonzero OOB offset still underflows.

The source-extracted admission check reproduces this with an eight-page
device plus one trailing byte, starting at the final partial page. The TLC
counterexample persists with either independent fix and with both combined.
The mathematical bounded policy rejects it. Neither existing patch is
claimed to fix this additional boundary case; it needs a follow-up to the
EOF OOB submission. The executable witness stops before any hardware access.
