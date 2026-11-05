# H616 provider-owned reset pulse

The 26-patch v10 draft at `dfc9d0414859` folds this change into recovery
patch 21, `dd432996b9e4`. The integration copy is `29da937720a8` under tip
`1bfc2aacac0b`. Both branches contain identical NAND driver bytes:

```text
ffd7225a3cb9680dfc6463a2e3cf842fa564b430c85a6b3a322826ea538a37bb
```

H616 probe and DMA-abort recovery now use `reset_control_reset()` instead
of separate assertion, a consumer-owned 2 microsecond delay, and
deassertion. The H616 CCU's `ccu_reset_reset()` performs the same register
transitions with its own 10 microsecond assertion interval. The reset is
mandatory, acquired and exclusive, so shared-reset once-only semantics do
not apply. Probe exercises the same provider operation needed by recovery.

Older controllers retain their optional-reset deassertion and existing
abort handling. Error unwind and removal still assert reset. IRQ draining,
event acknowledgement, caller-buffer ownership and recovery-error policy
are unchanged. The commit message and current v10 cover notes describe the
provider-owned pulse; historical sent revision notes are preserved verbatim.

## Source fixtures and model boundaries

The source fixtures execute the candidate's unchanged helper bodies.
The pulse fixture distinguishes framework rejection before the provider
callback from the CCU callback's successful assert/delay/deassert sequence.
It checks the 10 microsecond hold, repeated exclusive pulses, mapping
retention until return, and probe's use of the pulse API. Older source
snapshots keep their split-call fixtures and hypothetical phase failures.

`NandStreamingRecovery` retains assertion and deassertion as internal
provider phases. Its deassertion-error case is a hypothetical weakened
provider contract, not a reachable error return from the actual H616 CCU
callback. A reset-core error before assertion with DMA still active remains
an explicit unsafe-unmap counterexample. Reporting that error and rejecting
later commands do not themselves retire outstanding DMA accesses.

The reset framework's unsupported-operation error is kernel `-ENOTSUPP`
(524), distinct from userspace `-EOPNOTSUPP`; the pulse fixture checks exact
propagation alongside `-ENODEV`, `-EPERM` and a generic injected `-EIO`.

## Validation

- All 16 source-regression jobs pass with unchanged executable inputs.
- All 26 streaming-recovery model expectations match, including mutation
  and hardware/provider-assumption counterexamples.
- Actual abort helpers pass eight direction/chip/soft-reset cases with
  repeated mapping reuse and provider-owned pulse timing, plus 16 exact
  error-return boundary cases. Eight boundary cases deliberately retain
  the active-DMA unsafe-unmap witness.
- Lifecycle checks pass 1,272 cases and 86,555 assertions. The additional
  1,788 probe traces pass, including reset admission and unwind paths.
- The pinned v9 low-level and lifecycle checks still pass; they retain
  their original known-bug witnesses rather than being treated as fixed.
- ARM64 GCC and ARM32 LLVM `W=1` object builds pass for patches 21 through
  26 individually. Strict checkpatch reports no findings for patch 21;
  b4 checks pass all 26 patches. Export contains 27 messages and sends none.

Local generated reports are `results/suite-jw5f02wz/summary.json`,
`results/tlc-opqt40aw/summary.json` and
`results/lifecycle-ri_cjs78/summary.json`. Generated logs are not published.

The applicable Sashiko general technical, false-positive, locking, cleanup,
MM-allocation, MMIO-accessor and build guidance was checked against the
actual reset framework, CCU provider and DMA callers. There is no dedicated
NAND guide. No new regression was identified; the failed-reset/active-DMA
assumption above remains unresolved outside the supported provider contract.

## Hardware status

No fresh board run was performed for the provider-owned pulse change. The
[previous request-fault measurements](H616-REQUEST-FAULTS-20261105.md) used
the explicit 2 microsecond sequence and retain their original source digest.
They are not relabeled as tests of this revision. A fresh abort/reuse smoke
test is the remaining hardware check for the 10 microsecond provider pulse.
