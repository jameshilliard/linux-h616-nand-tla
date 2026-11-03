# Source-level findings and reachability

These tests preserve the submitted driver. They reproduce three conditional
source-level failures; they do not silently apply fixes or establish that the
OOB series introduced them. None has been observed on hardware by this suite.

## 1. Split address instructions lose the trailing address cycles

Location: `sunxi_nfc_exec_subop()` in the pinned
[driver](../reference/sunxi_nand.c).

The NAND-core helper returns the **number** of address cycles in the subop,
already excluding its start offset. The driver loops while
`j + start < remaining`, treating that count as an end offset. For the second
part of a ten-cycle instruction, `start = 8`, `remaining = 2`, so it emits no
address bytes.

The low-level test executes the unchanged NAND-core parser as well as the
driver. Check-only accepts the ten-cycle operation; execution issues two
commands and returns success, but emits only eight address cycles. This is not
merely an artificially constructed subop that the parser could never produce.

`command-source-split-address-count` reproduces the same failure of
`AddressCount` in TLC. Normal commands used by the NAND devices checked here
fit within eight cycles. A real supported NAND part or caller requiring the
larger instruction has **not** been identified. Treat this as a confirmed
parser/driver contract mismatch with unestablished normal-hardware reachability.

## 2. Failed clock changes are cached as successful

Location: `sunxi_nfc_select_chip()`.

The driver ignores the return from `clk_set_rate()` and updates `nfc->clk_rate`
to the requested chip rate. The source fixture injects `-EIO` and leaves the
physical rate unchanged. The cache then differs from the physical rate; a
second selection of that chip does not retry the clock change because the
cache appears to match. The new chip's timing registers are still installed.

`lifecycle-assumption-clock-set-fails` fails `CorrectChipContext` under this
condition. The host test confirms the ignored error and skipped retry in the
actual source. Supported clock-provider failure paths have not been fully
audited here, so this is not a claim of a reproduced H616 timing failure.

The separate `timing-assumption-faster-rounded-clock` test is a different
contract: the rounded clock must not invalidate the computed cycle counts.
That test is not evidence that the actual H616 clock rounds unsafely.

## 3. External DMA configuration failure does not disable DMA

Location: the external-channel branch of `sunxi_nfc_dma_init()`.

After acquiring the channel, the driver ignores `dmaengine_slave_config()`'s
return value. An injected `-EIO` leaves the channel installed and init returns
success. `probe-source-config-failure` reaches `DmaConfigured`'s counterexample;
the low-level C test executes this exact source path.

This applies to external DMAengine, **not** H616's descriptor-MDMA branch.
The test has not established that a supported provider rejects this driver's
four-byte bus widths, addresses and burst settings in normal operation. It is
a conditional provider-error handling finding, not a demonstrated H616 data
corruption report.

## Separate unresolved contracts

Ineffective abort/reset, command completion preceding DMA completion, PIO
timeout leaving a command active, and old-command interrupts arriving after
reuse have explicit counterexamples. These remain hardware/provider questions
until independently verified; they are not folded into the three source
findings above. Core-user draining and MMIO ordering are also assumptions.

The false-positive review distinguishes these classes deliberately. A formal
model can show what goes wrong **if** an assumption is removed without proving
that a supported implementation ever violates it.
