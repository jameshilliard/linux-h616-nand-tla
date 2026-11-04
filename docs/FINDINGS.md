# Source-level findings and reachability

These tests preserve the submitted driver and matching core. They reproduce
six conditional source-level failures; they do not silently apply fixes or
establish that the OOB series introduced them. None has been observed on
hardware by this suite.

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
until independently verified; they are not folded into the six source
findings above. Core-user draining and MMIO ordering are also assumptions.

The false-positive review distinguishes these classes deliberately. A formal
model can show what goes wrong **if** an assumption is removed without proving
that a supported implementation ever violates it.

## 4. Successful read retry can duplicate or retain incorrect OOB

Location: `nand_do_read_ops()` in the pinned
[NAND core](../reference/nand_base.c), not the sunxi driver.

The core copies a page's OOB and advances the output pointer before examining
the failed-ECC counter and deciding to retry. `goto read_retry` restores the
ECC statistics but not the OOB pointer or remaining OOB length. A successful
second attempt can therefore fill the next page's OOB slot with the retried
page's OOB. OOB copied from the failed attempt also remains in the first slot.

The exact-source fixture reads two pages with data and OOB. The first page's
initial attempt increments `ecc_stats.failed`; changing retry mode succeeds,
and the second attempt succeeds. The call returns success and full data/OOB
lengths, with correct main data and restored failed statistics, but duplicated
page-0 OOB instead of page-1 OOB. Both `AUTO_OOB` and `PLACE_OOB` reproduce it.
`integrated-request-source-retry-oob-*` fail `OobIdentity` for the same ordering.

This requires a chip with an actual `setup_read_retry` implementation and
additional retry modes, an ECC failure followed by a successful retry, and a
combined data/OOB request. OOB-only reads and raw reads do not exercise this
retry path. The suite has not established that the connected H616 NAND meets
those conditions or reproduced this on hardware.

## 5. BBT creation ignores a marker-scan transport error

Location: `check_create()` in the pinned [BBT core](../reference/nand_bbt.c).

When no flash BBT exists, `check_create()` calls `create_bbt()` but discards its
return value. A non-ECC transport failure stops the marker scan; the caller can
then write that incomplete RAM table as a new version-1 primary and mirror and
return success.

The source fixture injects `-EIO` while scanning block 1 and puts a physical bad
marker in block 2. `create_bbt()` returns the error, but `check_create()` returns
zero after publishing the tables. The unscanned bad block remains classified
good. Both OOB-header and `NAND_BBT_NO_OOB` in-band tables reproduce the failure;
the latter is the format sunxi selects when flash BBT is enabled.
`integrated-bbt-source-scan-error` fails `NoIncompletePublish`.

The conditions are flash-BBT creation with a marker scan, a transport error
before a bad block is scanned, and subsequent BBT writes succeeding. No new
driver bug or board failure is inferred merely from injecting that error.

## 6. BBT read transport errors can be accepted without mirror recovery

Location: the readback branches of the same `check_create()`.

`read_abs_bbt()` can return a transport error. `check_create()` handles
`-EBADMSG` by invalidating that copy and reconsidering the mirror, and
`-EUCLEAN` by requesting a rewrite, but does not reject other negative returns.
With matching primary/mirror versions and no pending rewrite it returns zero.

The source fixture first writes valid tables containing a worn-block entry,
then clears the RAM BBT to simulate remount and injects `-EIO` into the selected
primary read. The intact mirror is not used. `check_create()` returns success
while that bad-block entry is absent from RAM. Both in-band and OOB-header
formats reproduce it. `integrated-bbt-source-read-error` fails `NoFalseGood`.

The injected read obeys the MTD contract: negative transport status and no
returned bytes. Reaching that particular error after successful table discovery
is a conditional scenario, not an observed hardware event. The OOB series does
not change this core function, and this repository does not patch it.

## Other observed core behavior

The fixture also records the existing partial-write cache-invalidation rule:
the cached page is invalidated when its page-start address lies in the write
range. A write beginning later within that page can leave the cache marker.
This is not reported as a new sunxi finding: the driver sets
`NAND_SUBPAGE_READ`, which prevents this generic full-page read-cache path from
being populated. Likewise, write-error `oobretlen` behavior is checked as the
actual core contract, not treated as proof that failed physical programming
completed. Neither observation justifies a speculative driver change.
