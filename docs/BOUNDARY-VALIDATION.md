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
