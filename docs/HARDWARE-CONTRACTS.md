# Hardware contracts and evidence

This audit separates manual-backed register behavior, vendor usage, prior
board observations and assumptions that still need measurement. Linux source
is evidence of what the driver does, not an authority for what hardware does.
The [evidence manifest](../hardware-evidence.json) records exact documents,
hashes and vendor functions. PDFs and private capture files are not bundled.

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
| Descriptor length is 16 bits, eight-byte granular; zero skips the buffer | 5.2.3.4 | Length/alignment checks; zero-sized chains remain outside the model |
| Descriptor and buffer addresses are word aligned | 5.2.3.4 | Alignment matrix |
| User-data lengths are encoded per block and share a 128-byte bank | 5.2.6.21–22 | Batch/layout/register checks |
| ECC counters and pattern IDs are block-indexed | 5.2.6.20, 25 | Snapshot/cache/batch checks |

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

This model specifies a caller/register protocol, **not a mechanically extracted
copy of every Linux error path**. `NandIrq` retains the finer-grained handler
interleavings. `NandRecovery` and `NandRecoveryRegisters` retain the separate
failed-stop, outstanding-write and late-event questions. The synchronous C
fixtures immediately latch commands; they cannot establish asynchronous timing.

## Corrected transfer-count assumption

The earlier raw-H616 tests allowed an 8192-byte SRAM window to imply an
8192-byte normal command. That assumption was wrong. Previous passing results
for direct transfers above 1024 bytes are superseded, not silicon evidence.

The corrected fixtures decode the counter independently of the unmodified
driver. Ten TLC source-contract cases and twelve direct-buffer C cases expose
the discrepancy; ten model-only capped-chunk candidates show one algorithmic
way to preserve the transfer. No kernel fix is silently applied.

`python3 run_transport.py --count-boundary` reaches actual attach, page,
extra-OOB, command-helper, parser and IRQ code using an admitted **synthetic**
2 KiB+2 KiB geometry. Both read and write request an 1872-byte tail, transfer
848 bytes in the manual-based fixture, and return success. This also reproduces
against the v10 draft. No specific NAND part with that geometry is claimed.
See [finding 11](FINDINGS.md#11-h616-direct-buffer-io-confuses-sram-capacity-with-the-transfer-counter).

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
No flash write, erase, reset or power-cycle was performed for this audit.

Kernel-side findings were checked using Sashiko's general technical-pattern,
false-positive, callstack, MMIO/accessor, alignment and locking guidance.
The count issue is classified as a source/manual mismatch with a demonstrated
caller path, not an observed board failure or an automatically accepted bot
suggestion. Model/test changes do not make a kernel series ready to send.
