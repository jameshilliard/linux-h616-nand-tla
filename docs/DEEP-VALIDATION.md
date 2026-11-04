# Discovery, recovery, format and compound-failure validation

This extension validates the six follow-up areas against the same pinned NAND
OOB v9 implementation. It adds executable checks, not kernel changes. All
source references and model assumptions below are deliberate boundaries of the
result: executing every driver function is not an end-to-end kernel proof.

This records the deep-validation stage. The subsequent
[boundary extension](BOUNDARY-VALIDATION.md) replaces several of the decoder,
MTD-reference and provider IRQ/tasklet fixtures mentioned below with actual
source execution. Its findings also narrow the earlier user-draining assumption.

## Coverage map

| Area | Executed/model-checked | Still outside the result |
| --- | --- | --- |
| Discovery → attach → registration → cleanup | Actual `sunxi_nfc_probe/remove`, chip initialization/unwind, core `nand_scan_ident`, `nand_scan_tail`, `nand_scan_with_ids` and cleanup bodies. Fault every event on 20 fixture variants, including second-chip failure, extra-die mismatch, missing/duplicate/out-of-range selects, optional-clock combinations, ECC engines and allocation ownership. | ID/ONFI parameter-page decoders, manufacturer internals, device-core/devres/OF reference implementations and live users during unbind remain fixtures. The actual ECC geometry initializer is executed in the separate OOB harness. |
| Backend timeout recovery | `NandRecovery` separates the command, DMA producer, outstanding memory write, latched event and in-flight IRQ. PIO, sun4i/sun6i external DMA, register MDMA and descriptor MDMA profiles. Actual provider config/terminate entrypoints and actual NFC abort/reset/cleanup bodies execute in C. | Stop-register writes do not prove bus retirement or suppression of future controller events. Provider IRQ/tasklet/list internals and silicon reset behavior are not emulated. |
| Boot handoff and timing negotiation | Actual core reset/setup/features negotiation; `NandNegotiation` starts from bootloader SDR modes 0/3/5, then checks reset, modes 0/2/5, no-feature/set-only/get-feature devices, mismatch, transport failure and failed recovery. Existing timing arithmetic and shared-controller selection tests remain. | No electrical setup/hold measurement, NV-DDR bring-up or arbitrary clock-tree proof. A featureless SDR chip need not change a numeric timing-mode latch to tolerate faster cycles. |
| Cross-version on-flash format | Exact pre-series Linux OOB offsets/free regions and seed tables compared with the submission. Exact BSP normal-page spare-size block, packed user-length/register routines and page-seed selection compared in both directions. Actual sunxi-tools page writer checked against independent bytes. | Not all BSP releases, boot0 formats or flash-BBT formats are interchangeable. No captured hardware/BSP golden image is included. |
| BCH/randomizer composition | Independent GF(2^14) polynomial encoder compared with unmodified Linux BCH; actual decoding after physical-bit corruption; actual sunxi-tools page encoder; decoded messages and raw images fed through the driver's real PIO/DMA packing, BBM, randomizer and unprotected-OOB helpers. | Software agreement does not prove the hardware BCH polynomial, parity padding or correction behavior. No analog retention/wear model. |
| Compound failures | Actual core retry followed by transport error; actual BBT erase failure followed by replacement program failure, exhaustion, surviving-copy read and restart. `NandRequest` and `NandRelocation` explore error prefixes, replacement selection, mirror separation, power cuts and detectable/undetected torn tables. | An unacknowledged new bad-block mark need not survive failed persistence without an OOB marker. Torn-write detection is an ECC/device contract, not an invented commit record. |

## Lifecycle fault injection

`run_lifecycle.py` extracts 20 unchanged bodies: nine from the driver and eleven
from NAND core. The actual driver conversion, detach, chip-list and probe/remove
bodies close the earlier eight-function extraction gap. Allocation records
distinguish manual from managed resources and reject double frees, unowned frees,
live manual allocations after unwind, registered children after failure, and
unreleased DMA/reset resources. Successful paths remove both registered chips.

The fixture counts fallible events on each baseline path and replays with each
one failing. It does not claim all combinations of simultaneous allocation
failures or all manufacturer-specific scan paths. Its geometry discovery,
ECC-engine allocation, BBT scan and kernel services are named fixture
boundaries; BBT and ECC internals have separate source-executed harnesses.
All 20 extracted functions must execute in the separate GCOV run.

## Recovery contracts and provider scope

The positive backend models assume separately that:

1. Stop/reset prevents the DMA producer from issuing another transaction.
2. Outstanding writes have retired before unmapping.
3. The old NAND command cannot remain active across reuse.
4. An old completion cannot be latched or generated after reuse.
5. A handler already in flight is drained before reuse.

Each of the first four has a named assumption counterexample; removing IRQ
draining has a mutation counterexample. A successful IRQ drain alone cannot
prove the other four. PIO has no explicit DMA abort, external termination stops
the DMA provider rather than resetting the NAND controller, and integrated
MDMA uses the controller's reset helper. The model's `CommandStops` and
`EventsStop` contracts must not be mistaken for extra reset/clear calls in C.

The pinned sun4i and sun6i `*_dma_config()` bodies only copy the supplied
configuration and return zero. The earlier injected `slave_config()` error is
therefore **not reachable through these two configuration implementations**.
It remains an API-error-handling observation about the client, not evidence of
a failing supported configuration. Their terminate entrypoints write stop
registers, clear channel associations and return zero. sun4i also explicitly
masks channel interrupts; sun6i removes pending work/list state under its locks.
The host fixture checks those entrypoints' register writes and state changes,
not the real provider's IRQ/tasklet concurrency or hardware bus guarantees.
The NAND client installs no DMA completion callback in its descriptor.

## Independent codec and format oracles

Inputs are pinned under `reference/codec`, `reference/compat` and
`reference/providers`, each with its own source manifest. The older Linux source
is the series base; the BSP snapshot is
[Radxa's Allwinner BSP](https://github.com/radxa/allwinner-bsp/tree/2045a3ca2a01f088c0314dc924bda59d154e363e/modules/nand/common1/phy-nand/rawnand).
The image writer is from
[sunxi-tools](https://github.com/linux-sunxi/sunxi-tools/blob/d4320e2b5ea69ad3255ddeec0b2191755e6e59f8/nand-image-builder.c).
References retain their original licenses and are never rewritten by tests.

`codec_oracle.py` derives the binary generator polynomial from GF(2^14) roots,
then encodes by integer polynomial long division. It shares no encoder code or
remainder tables with Linux. Its bit-by-bit LFSR is separate from the driver's
word recurrence. Linux's decoder provides the correction implementation. The
sunxi-tools writer embeds an older BCH implementation with shared ancestry;
agreement between those two C implementations alone would not be independent
verification. The polynomial oracle supplies that independent check.

The codec matrix includes all strengths in the driver's A10/H6 tables, both
512/1024-byte messages, and 0/4/16/32 protected bytes. It injects exactly
0, 1, t/2 and t distinct codeword errors, plus selected 2t+1-error witnesses.
Unused padding bits are not counted as BCH symbols. In particular, the t=80
generator degree is smaller than the 14t-bit byte container. Correcting padding
bits is not promised. Beyond-capacity corruption can miscorrect; the suite
records the outcome rather than assuming all >t errors are detectable.

Driver-composition profiles use admitted controller capabilities: 512-byte
pages have one step and PIO only; larger pages use 1-KiB steps; legacy
controllers stop at strength 64. Four even-byte parity sizes avoid asserting
the value of a hardware-only alignment byte. Protected bytes are checked before
and after the plain-BBM transform, including non-erased marker values. Physical
errors are injected before descrambling and real BCH correction; the corrected
outputs are then loaded into the source-executed driver fixtures. Wrong-seed
decoding must reject or return different data, never silently satisfy the
original-message assertion. PIO/DMA raw parity representations are checked
separately; tails must decode identically.

The sunxi-tools normal-page writer uses four protected bytes per step and
overwrites the physical BBM after encoding. Linux's plain-BBM path compensates
the BBM **before** encoding. Consequently, these are not asserted to have
identical randomized parity for every marker. The writer is checked against an
independent reconstruction of its own format. BSP normal-page H6/H616 packing
is instead checked against the actual packed-length/user-data routines.
Generic per-step packing is deliberately shown to differ from BSP prefix
packing for multiple steps; the suite never declares those formats equivalent.

## Findings and review checks

The new timing test reproduces a conditional ignored-recovery-error path in
NAND core, detailed as finding 7 in [FINDINGS.md](FINDINGS.md). Retry-plus-I/O
failure extends the existing OOB-copying finding; it is not counted as a new
bug. The provider audit narrows finding 3 as described above. No production
source was changed and no new board failure was observed.

Review-driven scope includes the historical
[multi-chip scan discussion](https://lore.kernel.org/linux-mtd/20221229181526.53766-2-samuel@sholland.org/),
[probe cleanup work](https://lore.kernel.org/linux-mtd/20200519130035.1883-54-miquel.raynal@bootlin.com/),
and [randomizer discussion](https://lists.infradead.org/pipermail/linux-mtd/2015-October/062620.html).
These identify obligations; they do not substitute for source checks.

Applicable Sashiko guidance read and used: `review-core.md`,
`technical-patterns.md`, `false-positive-guide.md`, `callstack.md`, the subsystem
index, and `subsystem/locking.md`, `subsystem/io-accessors.md`,
`subsystem/alignment.md`, `subsystem/mm-alloc.md`, and `subsystem/of.md` from the
[public prompts](https://github.com/sashiko-dev/sashiko/tree/main/third_party/prompts).
There is no NAND-specific prompt in the inspected set. Caller/callee ownership,
error propagation, loop retry state and source-backed reachability were checked.
Unexecuted kernel-service boundaries are disclosed above rather than claimed
as completed callstack proofs. No Sashiko bot approval or kernel build is claimed.
