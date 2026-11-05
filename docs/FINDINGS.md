# Source-level findings and reachability

The baseline tests preserve the submitted driver and matching core. They reproduce
eleven source-level observations with explicit reachability limits; they do not silently apply fixes or
establish that the OOB series introduced them. None has been observed on
hardware by this suite.

Separate [candidate-fix checks](CANDIDATE-FIXES.md) now exercise local fixes
without replacing the pinned inputs. The observations below describe that
original baseline, not the candidate branches.

Later hardware-informed findings are recorded separately to preserve that
baseline's scope: [post-abort data ordering](POST-ABORT-DATAPATH.md) and
[large-tail stream composition](RECOVERY-COMPOSITION.md). The latter has an
actual-candidate-source reproducer on a synthetic admitted geometry, not a
claimed large-OOB physical-device reproduction.
The [reviewable v10 update](REVIEWABLE-V10.md) fixes that software-tail
mismatch while preserving the writer and stored format. Post-abort recovery
and the separate native non-word read-tail behavior remain unresolved.

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
The deeper audit executes the pinned sun4i and sun6i configuration callbacks:
both only copy the configuration and return zero. Thus the injected error is
not reachable through either of those implementations. Retain it as a
conditional client/API robustness observation, not a bug demonstrated with
these providers or a H616 data-corruption report. This narrowing is based on
the actual callback bodies, not a presumption that configuration usually works.

## Separate unresolved contracts

Ineffective abort/reset, command completion preceding DMA completion, PIO
timeout leaving a command active, and old-command interrupts arriving after
reuse have explicit counterexamples. These remain hardware/provider questions
until independently verified; they are distinct from the source findings.
The earlier core-user-draining assumption is narrowed by finding 8 below;
MMIO ordering and physical bus retirement remain hardware contracts.

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

The compound-error test also retries page 0 successfully before page 1 returns
`-EIO`. Main-data `retlen` covers only page 0, while OOB accounting has consumed
two slots. Both `AUTO_OOB` and `PLACE_OOB` reproduce this; raw reads do not retry.
`validation-compound-retry-transport-oob-*` fail `OobErrorPrefix` accordingly.

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

## 7. Timing-mode mismatch can hide a failed recovery

Location: `nand_setup_interface()`'s `err_reset_chip` path in the pinned
[NAND core](../reference/nand_base.c).

A successful GET_FEATURES returning a different timing mode intentionally
falls back to SDR mode 0. That fallback is not itself a bug. However, the core
ignores both `nand_reset_interface()` and `nand_reset_op()` return values and
returns the earlier GET_FEATURES status, zero in the mismatch case.

The lifecycle runner executes the unmodified function with either a failing
controller mode-0 setup callback or a failing RESET transport call. It returns
zero even though recovery failed. The fixture's host or chip mode can remain
at the previously selected mode; the current-interface pointer has already
been changed to the reset configuration. `NandNegotiation` independently
reproduces `RecoveryError` for both cases.

This requires a timing-acknowledgment mismatch **and** a recovery failure.
A GET_FEATURES transport error already returns nonzero and is not confused
with this case. Actual H616 mode negotiation/reset failure has not been
reproduced, and supported-provider reachability of a failing mode-0 setup is
not established. The source handling is confirmed under these callback
contracts; it is not attributed to the sunxi OOB series.

## Other observed core behavior

The fixture also records the existing partial-write cache-invalidation rule:
the cached page is invalidated when its page-start address lies in the write
range. A write beginning later within that page can leave the cache marker.
This is not reported as a new sunxi finding: the driver sets
`NAND_SUBPAGE_READ`, which prevents this generic full-page read-cache path from
being populated. Likewise, write-error `oobretlen` behavior is checked as the
actual core contract, not treated as proof that failed physical programming
completed. Neither observation justifies a speculative driver change.

## 8. Open MTD references do not defer sunxi resource cleanup

Locations: `del_mtd_device()`, `mtd_device_unregister()`,
`__del_mtd_partitions()` and `sunxi_nand_chips_cleanup()` in the pinned source.

The actual MTD removal functions drop registration references; they do not
wait for open references to reach zero. The sunxi caller immediately invokes
`nand_cleanup()` after unregister returns. The new lifetime harness executes
these bodies with one to three nested partitions, zero to two open users and
both settings of `CONFIG_MTD_PARTITIONED_MASTER`. In 12 of the 36 schedules,
unregister succeeds and sunxi cleanup runs while the open reference remains.

This invalidates the earlier blanket assumption that MTD unregister itself
drains users. `NandMtdLifetime` demonstrates why registration/refcount ownership
alone cannot keep resources safe during an active operation. A separate positive
configuration explicitly excludes unbind until the users have closed.

Additional source inspection traces `mtdchar_open()` retaining an MTD handle
until `mtdchar_close()`. The driver's platform registration does not suppress
bind attributes; `unbind_store()` reaches device removal, followed by devres
cleanup. A module reference is not the same as exclusion of administrative
driver unbind. The source path therefore warrants a separate lifecycle audit.

The host test represents `nand_cleanup()` resource release with an ownership
flag and retains object storage for inspection. Its exact allocation/unwind
body is tested separately in `run_lifecycle.py`. This is a confirmed mismatch
between reference lifetime and immediate resource teardown, **not a measured
use-after-free on hardware or a full concurrent sysfs test**. The OOB series
does not introduce the cleanup sequence.

The [refinement tests](REFINEMENT-VALIDATION.md) extend this witness to actual
`mtd_read_oob_std()` dispatch and `get_mtd_device()` lookup. In the 12 retained
partition cases, a later I/O reaches the cleaned resource and a further open
can succeed while the old reference keeps the MTD published. Four two-chip
schedules add four later-I/O witnesses. Actual `mtd_release()` removes the
IDR entry when the final reference is dropped; subsequent lookups then fail.
`NandDetachPolicy` checks alternative entry-gating/resource-lifetime policies,
including partial probe publication. Those policies are not implemented fixes.

The [file-lifetime extension](FD-LIFETIME-CONFORMANCE.md) follows actual
`mtdchar_close()` through `mtd_sync()` and `nand_sync()`. A writable close
locks the chip and controller before dropping its retained MTD reference.
Four bounded schedules therefore reach already-cleaned lock storage even
without another explicit I/O. This is another path through finding 8, not a
new independent defect. Any entry-gating policy must account for close/sync
as well as reads and writes. Real device-core/devres concurrency remains a
separate validation boundary.

## 9. The pinned sun6i DMA IRQ dispatch mishandles upper banks

Location: `sun6i_dma_interrupt()` in the pinned
[provider](../reference/providers/sun6i-dma.c).

The outer loop uses `num_pchans / DMA_IRQ_CHAN_NR`, with eight channels per
bank. The inner loop selects `sdev->pchans + j` without the bank offset.
Consequently, with 16 channels an event for channel 8–15 is dispatched to
channel 0–7. With 12 channels, integer division omits the upper partial bank
entirely. The source contains 16-channel A31 and 12-channel H3 configurations;
the counts are not invented invalid inputs.

The fixture starts real provider descriptors on each channel, injects each
upper-bank event, then executes the unchanged IRQ handler. It observes eight
wrong descriptor-cookie completions and four unvisited events. The independent
`NandProviderBanks` arithmetic model reproduces both cases. Its corrected-index
reference configurations are model oracles, not edits to the kernel source.

This finding concerns the pinned provider and a transfer allocated an upper
physical channel. It does not establish that the NAND controller on a given
board uses those channels. H616 NAND's integrated descriptor-MDMA path does not
use this provider. Current upstream/provider fix status has not been inferred
from this snapshot, and the OOB series does not change the IRQ handler.

## 10. sun4i cyclic termination can retain a freed callback descriptor

Locations: `sun4i_dma_interrupt()`, `sun4i_dma_terminate_all()` and
`vchan_complete()` in the pinned provider/virt-dma source.

A cyclic interrupt can queue `vc.cyclic` for the virt-dma tasklet. If termination
runs before that tasklet, the sun4i path collects and frees the processing
contract without clearing the queued cyclic pointer. The later tasklet obtains
the callback from that contract. The source-extracted test reproduces this for
both normal and dedicated physical channels.

The fixture marks the contract logically freed but retains storage, allowing
the callback to record the ownership violation without intentionally causing
a host use-after-free. Its two witnesses are distinct from an already-running
callback race: the tasklet has not started when the contract is freed.
Non-cyclic schedules and NULL/non-NULL callback variants are checked as well.

**sunxi NAND does not use cyclic DMA and installs no completion callback.**
This is a provider finding discovered while extending the validation, not a
NAND data-path failure or evidence that the OOB patches need cyclic support.
No production fix is applied and later provider revisions were not assumed
to have the same behavior.

## 11. H616 direct-buffer I/O confuses SRAM capacity with the transfer counter

Locations: `sunxi_nfc_read_buf()`, `sunxi_nfc_write_buf()`, their randomizer
wrappers, and `sunxi_nfc_hw_ecc_{read,write}_extra_oob()`.

Both H6 and H616 manuals define `NDFC_CNT[9:0]`: zero encodes 1024 bytes.
The pinned vendor normal-command sender explicitly masks its length to
`0x3ff`. The Linux buffer helpers instead chunk by `caps->sram_size`, which
is 8192 on H616, write the full chunk length to CNT and advance the software
offset by that length. SRAM capacity does not extend the counter field.
The normal operation parser's 1024-byte split does not protect direct callers.

The revised low-level fixture reproduces false success for direct reads and
writes of 1025, 2048, 8191, 8192, 8193 and 16385 bytes. The latter also exercises
several software chunks. An 8192-byte command transfers only 1024 bytes under
the documented encoding; a 1025-byte command transfers one. On reads, the
remaining software copy includes stale SRAM. On writes, data is omitted.

The stronger `run_transport.py --count-boundary` test uses actual attach and
full PIO page callbacks, including the extra-OOB helpers and command transport.
An admitted synthetic 2 KiB page with 2 KiB OOB, BCH32/1024 and 32 user bytes
per step leaves a tail of `2048 - 2 * (56 + 32) = 1872` bytes. The page
callback reports success while the fixture transfers `1872 & 0x3ff = 848`
tail bytes. Both directions reproduce it in unchanged posted v9 and candidate
v10. The attached board's 2 KiB+128 geometry does not have this tail.

This is a confirmed **source/manual contract mismatch with a demonstrated
driver caller path**, not a silicon reproduction or identification of a
physical affected NAND part. Admission under a synthetic geometry alone is
not evidence of real-world incidence. A real large-tail configuration or a
controlled direct-command probe would narrow that remaining question.

Earlier raw-H616 positive checks copied the software count directly into the
emulated hardware and missed this problem. They are explicitly superseded;
the new negative expectations do not mean the driver is fixed. Ten TLC
model-only capped-chunk candidates were separate from the unchanged source.
Hardware provenance and unresolved recovery contracts are in
[HARDWARE-CONTRACTS.md](HARDWARE-CONTRACTS.md).

**Candidate resolution (2026-11-04):** v10 commit `2fb2a7138fa1`
(`mtd: rawnand: sunxi: limit buffer transfers to the command byte count`)
uses the minimum of remaining bytes, the SRAM capability and 1024. The
parser shares the named limit. This is a separate existing-format fix before
the binding/format extension, with a Fixes tag for H616 support. Explicit
positive candidate checks now transfer the complete tail and preserve
chunk offsets/randomizer configuration; baseline expectations still reproduce
the bug. The H616 manual's two 1 KiB banks are also distinguished from the
driver's 8 KiB capability, rather than assuming that capability is silicon
documentation. No new hardware result or actual affected NAND part is claimed.

## 12. sunxi scans NAND children marked unavailable in DT

`sunxi_nand_chips_init()` uses the all-child iterator. Neither its
`sunxi_nand_chip_init()` callee nor the core scan path checks node availability
before claiming chip selects, detecting the NAND and registering its MTD.
Consequently, a disabled but populated child becomes usable; a disabled,
absent second child can fail the entire controller probe and remove the
previously registered first child.

The lifecycle harness executes the real initialization and scan bodies with
seven status variants, either child position and presence/absence: 28 cases
produce 16 unavailable-child scan witnesses in both posted v9 and the
pre-fix v10 snapshot. The OF availability and physical-presence services
remain explicit fixtures. `NandChildAdmission` independently checks a model-only filtered
policy and requires the current-ordering instances to fail
`OnlyAvailableScanned`.

This is an independent sunxi DT-admission bug, also present before the OOB
series. Filtering with the scoped available-child iterator before allocating
chip selects is a fix candidate; no kernel fix is applied in this extension.

**Candidate resolution (2026-11-04):** standalone commit `c8a746138a9d`
uses the available-child scoped iterator on the pre-series NAND base. The
source-extracted checks now confirm that unavailable children neither reach
detection nor claim chip selects. The separate publication-race witness remains
when this fix is tested alone. See the candidate section in
[PROBE-VISIBILITY.md](PROBE-VISIBILITY.md).

**Series integration (2026-11-04):** the same fix is now patch 20 of sunxi
v10 (`f79cdf7e993b`), with its standalone submission branch archived.
The combined series passes the positive lifecycle checks for both probe fixes.

## 13. Per-child publication allows live I/O to overlap the next NAND scan

`sunxi_nand_chip_init()` scans and registers each child before the outer loop
starts the next. MTD users can therefore access the first chip while the
second enters `nand_scan_ident()`. Normal I/O takes `controller->lock`, but
the identification path does not. `nand_detect()` issues RESET/READ ID through
sunxi operations that replace the shared controller's selected chip and
timing. A first-chip command can then launch under the second chip's context.

The lifecycle fixture executes the actual core lock acquisition for the first
published chip and reaches the second scan's detector with that lock still
held. Twelve low-level schedules then execute the real selector, `exec_op()`
and raw-buffer helpers, observing the wrong CS/timing at command launch across
A10/A23/H616 profiles. `NandProbePublication` reproduces the interleaving and
checks a model-only scan-before-publication policy separately.

This is a successful-probe race between separate child nodes, not the known
MTD-unbind lifetime issue. Source inspection of registration, MTD open/read
and NAND detection confirms that the parent's probe lock does not exclude
the user path. It is present before the OOB series and in both tested driver
versions. The evidence is source control flow and host schedule/register
witnesses, not an observed board failure or a real concurrent kernel test.

Deferring MTD publication until all chips are scanned is an independent fix
direction, requiring corresponding scanned/registered cleanup bookkeeping.
No kernel implementation is included. Detailed caller paths, model bounds,
review checks and remaining gaps are in [PROBE-VISIBILITY.md](PROBE-VISIBILITY.md).

**Candidate resolution (2026-11-04):** standalone commit `f962731e99f6`
completes all scans before registering any MTD and records successful
registration separately from scan completion. Fault-injected source checks
cover partial scanning, partial registration and cleanup, while an actual
core-lock acquisition at publication no longer overlaps identification.
The availability bug remains when this fix is tested alone. These checks do
not resolve retained MTD users during removal or constitute hardware testing.

**Series integration (2026-11-04):** the same fix is now patch 21 of sunxi
v10 (`fa01a63067b1`), following the availability fix and preceding the binding
and randomized-OOB patches. Its standalone submission branch is archived.
Only fixes outside the sunxi NAND driver retain independent submission branches.
