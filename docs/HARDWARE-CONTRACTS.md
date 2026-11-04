# Hardware contracts and evidence

This audit separates manual-backed register behavior, vendor usage, prior
board observations and assumptions that still need measurement. Linux source
is evidence of what the driver does, not an authority for what hardware does.
The [evidence manifest](../hardware-evidence.json) records exact documents,
hashes and vendor functions. PDFs and private capture files are not bundled.

**New hardware evidence:** the [2026-11-04 measurements](HARDWARE-MEASUREMENTS-20261104.md)
confirm the H616 counter/two 1 KiB banks and supersede the continuous-stream
randomizer assumption. Normal commands restart streams, page-size reads and
writes differ, and non-word read tails have an exception. Historical
assumptions below must be read with that correction; older NFCs are unmeasured.

The [extended v10 capture](HARDWARE-EXTENDED-20261104.md) adds native PIO/MDMA,
all on-chip BCH modes as isolated codewords, metadata reuse, public callers,
randomizer toggles and real timeout-status rechecks. It also finds that the
first read after an active abort can be incorrect despite reset-bit clearance
and quiet retained DMA memory. The models must not equate those observations
with fully restored data-path state. The [post-abort investigation](POST-ABORT-DATAPATH.md)
isolates a controller data-path problem: raw data can be displaced, clearing
SRAM does not fix it, and AHB bus reset prevents it in the tested comparison.
The precise internal state remains unidentified. Normal-command SRAM/count
observations apply to a clean controller; post-abort short commands can modify
SRAM outside the requested prefix. `NandPostAbortRequest` keeps these recovery
contracts explicit rather than assuming that reset-bit clearance restores them.

## Manual-backed register contracts

Section numbers below refer to the
[H616 manual](https://linux-sunxi.org/images/2/24/H616_User_Manual_V1.0_cleaned.pdf).
H6 revision 1.1 independently agrees on status and transfer-count semantics
in sections 4.2.5.2 and 4.2.5.9.

| Contract | Manual section | Validation |
| --- | --- | --- |
| CNT is ten bits; zero means 1024 bytes, independent of SRAM capacity | 5.2.6.9 | `NandCommand`, low-level and transport fixtures |
| One FIFO word can become free while the command FSM remains busy | 5.2.6.2 | `NandRegisterProtocol`, low-level busy-FSM case |
| CMD/DMA events clear on W1C or new FSM command start | 5.2.6.2 | Register model and host fixtures |
| Ready/busy edge remains latched until W1C; interrupt enable is separate | 5.2.6.2–3 | Register model, IRQ mask/status matrix |
| MDMA status has 32 W1C descriptor-completion bits, unlike the three-event NDFC status | 5.2.6.29 | Low-level selective-clear check |
| WAIT_FLAG selects readiness waiting | 5.2.6.10 | Register model and command/parser cases |
| Descriptor length is 16 bits, eight-byte granular; zero skips the buffer | 5.2.3.4 | Length/alignment checks; aperture model exposes the positive-length caller precondition; zero-sized chains remain outside scope |
| Descriptor and buffer addresses are word aligned | 5.2.3.4 | Alignment matrix |
| User-data lengths are encoded per block and share a 128-byte bank | 5.2.6.21–22 | Batch/layout/register checks |
| ECC counters and pattern IDs are block-indexed | 5.2.6.20, 25 | Snapshot/cache/batch checks |

## Older NFCs are separate profiles

The source capability structs are extracted independently for A10, A23 and
H616. They are not three names for one H616 register emulator. The
[A10 manual](https://raw.githubusercontent.com/allwinner-zh/documents/69a71a4321dadc2d1f99d7695161d7b2cd8540c4/A10/A10%20User%20manual%20V1.5%2020130820.pdf)
only contains an NFC overview (§14.1). It documents two 1 KiB banks and
external DMA, but omits the register chapter. The A23 manual download was
unavailable; the pinned Allwinner `nand_sun8iw3` BSP is used for its register
and programming differences. No missing manual has been treated as read.

| Contract/profile | A10 | A23 | H616 |
| --- | --- | --- | --- |
| Normal buffer command | Valid 1..1024-byte requests; raw CNT width/readback not established | Same valid-request contract, not borrowed H616 bit-width semantics | CNT[9:0]; zero encodes 1024 |
| RAM0 used by normal commands | 1 KiB bank, with another bank at +0x800 | BSP banks start at +0x400/+0x800; tests stay within the 1 KiB RAM0 window | Manual §5.2.1 also describes two 1 KiB banks; Linux's 8192 capability is not physical proof |
| Page DMA | External DMA provider | Internal address/count registers at +0xc0/+0xc4 | Descriptor-based internal DMA, list base +0x200 |
| Page block selection | Count | Count | Bitmap; separate from the byte counter |
| IO_DATA / SPARE_AREA offsets | +0x30 / +0xa0 | +0x300 / +0xa0 | +0x300 / +0x114 |
| ECC counters / user-data base | +0x40 / +0x50 | +0x40 / +0x50 | +0x50 / +0x80 |
| Protected user data | One 4-byte word per step | One 4-byte word per step | Packed per-step lengths; shared 128-byte bank |
| ECC error fields / random enable | 16-bit error bitmap / ECC_CTL bit 9 | 16-bit error bitmap / ECC_CTL bit 9 | 32-bit error bitmap / ECC_CTL bit 5 |
| Command-start event clearing | Test both alternatives; not established by available docs | Test both alternatives; not established by available docs | CMD/DMA auto-clear at FSM start is documented |

Older offsets, the sector count, four ECC-counter words, fixed user-data
words and DMA programming are cross-checked against the vendor's
[`nand_sun4i`](https://github.com/allwinner-zh/linux-3.4-sunxi/tree/6964d467510849e3e262518cb87bff7ef92e01f5/modules/nand/nand_sun4i/lib/src)
and
[`nand_sun8iw3`](https://github.com/allwinner-zh/linux-3.4-sunxi/tree/6964d467510849e3e262518cb87bff7ef92e01f5/modules/nand/nand_sun8iw3/lib/src)
headers, `_read_in_page_mode()`, `_check_ecc()` and DMA functions. Their
normal-command senders write the requested count without the newer BSP's
explicit `0x3ff` mask. That is evidence of usage, not proof of a wider counter.
The low-level fixture keeps valid older requests as lengths and makes no
assertion about raw CNT readback. The changed code issues at most 1024 bytes
on every profile, so that unresolved encoding does not affect its test result.

The fixture also checks extracted capability values against the separate
register layouts; older pattern interpretation remains a Linux/BSP contract
without a complete older register manual. Existing ECC/batch tests retain
16-slot/fixed-word and 32-slot/packed-bank distinctions. Legacy 512-byte ECC
blocks, H616 1024-byte blocks, and descriptor-only alignment/length rules are
not conflated. A23 uses the register-DMA tests, not H616 descriptor rules.

## Vendor cross-check

The pinned [vendor implementation](https://github.com/radxa/allwinner-bsp/blob/2045a3ca2a01f088c0314dc924bda59d154e363e/modules/nand/common1/phy-nand/rawnand/controller/ndfc_base.c)
masks normal-command lengths to `0x3ff`, checks FIFO and FSM separately,
and has distinct command, DMA and ready/busy waits. Its fetched bytes match
the repository's recorded digest. These are corroborating usage patterns,
not proof that every BSP branch describes the same RTL or handles errors
correctly. Vendor code is not copied into the Linux implementation.

## Register-model scope

`NandRegisterProtocol` explores two operations with distinct enqueue, hardware
latch, FSM completion, DMA completion, readiness, mask/observe and retirement
actions. Event bits coalesce rather than forming a completion queue. Initial
stale events expose the interval before the next hardware latch. The positive
caller clears relevant old events and waits for its required completions.

PIO/internal-DMA, polling/interrupt and readiness profiles are checked with
three DMA/command ordering envelopes. Allowing either completion order is a
conservative adversarial abstraction; the manual does not establish every bus
transaction's ordering. Passing does not prove the external DMA provider's
memory-drain contract. The deliberate failures cover relying only on automatic
clearing, equating FIFO availability with completion, ignoring DMA completion,
and assuming interrupt masking clears status.
Eight additional older-profile configurations check polling/interrupt operation
with and without automatic event clearing. A10's external-provider completion
ordering remains in the separate provider/ownership models; it is not proved
by a controller-only status model.

This model specifies a caller/register protocol, **not a mechanically extracted
copy of every Linux error path**. `NandIrq` retains the finer-grained handler
interleavings. `NandRecovery` and `NandRecoveryRegisters` retain the separate
failed-stop, outstanding-write and late-event questions. The synchronous C
fixtures immediately latch commands; they cannot establish asynchronous timing.

## Corrected transfer-count assumption

The earlier raw-H616 tests allowed an advertised 8192-byte SRAM size to imply an
8192-byte normal command. That assumption was wrong. Previous passing results
for direct transfers above 1024 bytes are superseded, not silicon evidence.

The counter and RAM0 bounds are now independent of the driver's SRAM limit.
Ten TLC source-contract cases and twelve direct-buffer C cases retain the
count discrepancy, with permissive backing storage solely to isolate it.
Two additional source-contract cases reject crossing the manual's 1 KiB
bank boundary. Positive capped-chunk cases use the stricter 1 KiB bank.
Sixty-six generation-specific cases cover zero/short/exact/beyond-limit
transfers and errors in the first, second or third command.

`python3 run_transport.py --count-boundary` reaches actual attach, page,
extra-OOB, command-helper, parser and IRQ code using an admitted **synthetic**
2 KiB+2 KiB geometry. Both read and write request an 1872-byte tail, transfer
848 bytes in the manual-based fixture, and return success. This reproduced
against the previous v10 draft. No specific NAND part with that geometry is claimed.
See [finding 11](FINDINGS.md#11-h616-direct-buffer-io-confuses-sram-capacity-with-the-transfer-counter).

The updated v10 includes a separate count-limit fix. Explicit
`--expect-count-fixed` checks require full transfers and do not reinterpret
counterexamples as success. Direct-helper tests now cover all three backends,
plain/scrambled data, two page seeds, exact chunk counts, untouched sentinels,
and later-chunk faults. Composed page tests compare the full H616 1872-byte tail
and the legacy 1928-byte tail in both directions. The pinned v9 remains unchanged.

## Prior hardware observations, narrowly applied

These are reviewed local records, not new tests or public raw captures. Their
hashes identify the archived artifacts; hashes alone do not make them publicly
reproducible. No device identifiers, addresses or NAND contents are published.

- **Column experiment, 2026-09-14:** 192 read-DMA operations plus 24 priming
  operations on 2 KiB+128 NAND. All 48 nonzero-start/two-slot trials read the
  later slot's valid prefix from the page-relative position. This supports the
  later-slot batching restriction. It does not establish write sequencing,
  sparse masks, larger pages or behavior on other SoCs. The v5 test driver was
  earlier than the pinned submission.
- **Delayed IRQ, 2026-09-12:** 355 injected cases across full/partial DMA
  completion and PIO setup/data/ECC waits. An 80 ms handler delay exceeded a
  20 ms wait, exercising handler draining. Hardware had already completed:
  this is not evidence that reset stops a hung DMA engine or retires writes.
- **v9 read smoke test, 2026-10-17:** 320 MiB of reads, about 15.6–15.7 MiB/s,
  plus data/OOB/subpage/marker and erased/zero-page checks. It did not exercise
  writes, erase, fault recovery, the larger tail or the updated v10 source.

## Still unresolved

Reset self-clear does not by itself establish bus retirement, reset-domain
coverage or the absence of late events. IRQ draining does not stop hardware.
External DMA completion ordering, recovery after real controller timeouts,
large-OOB silicon behavior, analog ECC characteristics and power-failure
semantics still require provider documentation or targeted hardware tests.
The physical extent/aliasing of H616's advertised 8 KiB aperture is unresolved;
this fix needs only the documented 1 KiB bank and command range. Randomizer
continuity was an explicit assumption in this audit; later H616 measurements
above disprove it for default block-size mode. Avoiding software reseeding
does not prevent hardware reloads. Older-controller behavior is unmeasured.
The [stream/aperture/context extension](STREAM-APERTURE-CONTEXT.md) now checks
the alternative reload behavior explicitly: same-chunk write/read round trips
can pass even when the independently expected stored stream differs. It also
checks DMA range endpoints and length-register reuse without borrowing the
H616 descriptor or packed-length format for older NFCs.
No flash write, erase, reset or power-cycle was performed for this original
audit. The separately recorded 2026-11-04 experiment did erase/program and
restore one authorized backup block.

Kernel-side findings were checked using Sashiko's general technical-pattern,
false-positive, callstack, MMIO/accessor, alignment and locking guidance.
The count issue is classified as a source/manual mismatch with a demonstrated
caller path, not an observed board failure or an automatically accepted bot
suggestion. Model/test changes do not make a kernel series ready to send.
