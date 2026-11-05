# H616 abort recovery: implementation and evidence

The 26-patch v10 candidate now contains an H616-specific recovery fix for
the [observed first-read-after-abort corruption](POST-ABORT-DATAPATH.md).
This supersedes the unresolved implementation status in the earlier reports;
their original captures, failures and hardware limitations are preserved.

## Small, separately reviewable changes

| Patch | Purpose |
| --- | --- |
| 20: own descriptor-DMA buffers | Isolate DMA from the page caller's lifetime; allocate a private coherent data buffer and explicitly owned descriptor; copy writes before publication and reads after successful completion |
| 21: reset the H616 data path after DMA aborts | Drain IRQs, attempt soft reset, assert/deassert the owned exclusive AHB reset, then restore context through the next chip selection; block further I/O if recovery fails |

Patches 1-19 are the preceding preparation and existing-driver fixes.
Optimizations follow at 22-24, then binding/format support at 25-26. The
recovery fix does not depend on the Allwinner OOB format. Its `Fixes:` points
to the original H616 descriptor-DMA support, `5ee37db34637`.

The draft tip is `6e32068f8449`; its NAND driver is byte-identical to the
hardware integration branch's driver. The final driver SHA-256 is:

```text
7f93090564302e65e251c48d7a700ec4d20b1b46e2a29d4f777e747f638348cc
```

The tested image predates two continuation-indentation fixes only, with hash
`42b063bbdafd7ca5b1b592c411df84c8a7fcd7d2896b279c78775b195572df7a`.
The diagnostic build adds a one-shot read-timeout injection and observation
counters; neither is part of the kernel patches. The source checkers use
the uninstrumented source. The posted v9 reference remains unchanged.

## Recovery and ownership

The H616 sequence is:

1. Disable/drain the IRQ, mask controller events and flush that write.
2. Attempt controller soft reset, then assert the exclusive AHB reset.
3. Hold reset for two microseconds, deassert it, clear pending events and
   re-enable the IRQ.
4. Let the next NAND operation select its chip and restore page layout and
   timing. A failed DMA read restarts as a whole-page PIO read; a failed
   write returns an error without replaying programming.

The private buffer covers the maximum admitted main-data transfer, 32 KiB.
No descriptor points at the caller's buffer. If the reset handle is missing
or assertion fails, recovery closes the controller to subsequent operations
and deliberately retains both allocations until reboot. If assertion succeeds
but deassertion fails, I/O remains blocked and the IRQ remains disabled;
allocation release is safe only under the stated reset-retirement contract.
Code paths returning a recovery error avoid further NFC register accesses.
The older external-DMA and register-MDMA abort sequences remain unchanged.

This choice adds one copy per descriptor-DMA batch. It exchanges some normal
path cost for bounded ownership after a failure that cannot retire DMA.
It is not a DMAengine-wide recovery policy.

## Actual-driver hardware test

The earlier bus-reset experiment called a diagnostic recovery helper. The
new operation instead launches the driver's ordinary MDMA page read and
injects a timeout at its wait helper. The **actual driver** performs abort,
IRQ drain, AHB reset, cleanup and whole-page PIO retry.

The injected delays are 0, 1, 10 and 1000 microseconds. They sample early
abort and potentially already-completed transfers; the test does not assert
that every injection interrupts an engine still active at that instant.
Each trial primes a third, distinct synthetic page. After the recovered
read it reads the same or a different page twice, through raw, PIO, MDMA or
public MTD access. Six repetitions give **192 recovery trials and 576 read
observations**. All matched independently derived synthetic data; recovered
reads reported no corrected or failed ECC. Each recorded exactly one PIO
retry, one DMA preparation and cleanup, and intact caller-buffer guards.

The diagnostic wrapper saves/restores selected registers and MTD statistics
between calls. Crucially, the recovered read and its PIO retry finish inside
the driver **before** that wrapper restoration. Follow-up captures still
exercise the wrapper, so they are not a complete unattended production-I/O
trace. The ordinary read after unloading the module is a separate check.

An additional **2,000 alternating PIO/MDMA reads** passed without data or
guard failures, unexpected fallback or failed ECC. These were separate
stress reads, not 2,000 additional injected aborts.

Testing used an independently booted RAM environment with UBI detached.
Only an explicitly authorized secondary SPL backup block was rewritten.
Fresh independent backups were verified first; each matrix restored that
block. All four complete SPL copies subsequently matched independent raw
reads. The diagnostic module was unloaded, an ordinary 32 MiB read passed,
and the board was returned to its normal image. No program/erase operation
was interrupted and no diagnostic image was flashed to production storage.

The [sanitized capture](../hardware/h616-native-abort-20261104.json) contains
synthetic hashes, register/counter values and restoration booleans only.
Offline replay reconstructs expected logical and BCH/LFSR physical data,
requires every matrix profile exactly once, and checks twelve deliberate
capture mutations. Private backups, credentials, board addresses and boot
images are not published.

## Models and source checks

`NandPrivateRecovery` separates soft-reset retirement, bus-reset retirement
and restoration of internal data ordering. Its **41 configurations** include
32 positive read/write, reset-error, chip-switch and soft-reset-contract
combinations, three weakened-hardware-contract counterexamples and six
deliberate policy regressions. Required witnesses cover premature free,
direct DMA into returned caller storage, unsafe reopen, missing context,
timeout-data copying and write replay. Successful API return alone is not
treated as observational proof of the physical reset contracts.

The low-level C runner executes the actual allocation, preparation, abort,
chip-selection and release helpers across **24 direction/chip/error cases**,
including repeated successful reuse, allocation failure, missing reset,
assertion/deassertion errors and a soft-reset failure rescued by bus reset.
It checks private DMA addresses, write copying, absence of caller mapping,
IRQ state, restored registers, no post-failure selection writes, and either
balanced frees or deliberate retention. ASan/UBSan and coverage runs agree.

Existing whole-request tests still contain abstract abort adapters; they
must not be described as complete C-to-TLA+ refinement of this new recovery
path. The new exact-helper checks, policy model and physical request test
provide complementary evidence. Final run records are in
[VALIDATION.md](../VALIDATION.md).

## Limits and performance

- The H616 CCU reset callbacks normally return success after writing the
  reset register. Missing-handle and API-error branches are host-injected,
  not reproduced by making the physical CCU fail.
- The test aborts reads deliberately; it does not reproduce a genuinely
  wedged interconnect, establish a universal outstanding-write retirement
  guarantee, or identify the internal RTL state responsible for displacement.
  Weaker retirement/data-reset assumptions deliberately fail the model.
- Recovery from an interrupted program, general PIO timeouts, older NFC
  silicon and the separately documented MTD-unbind lifetime issue are not
  established by these results. The native non-word randomized read quirk
  also remains distinct.
- Repeated 32 MiB raw-MTD reads took 2.45-2.48 s (0.70-0.72 s system time)
  on the diagnostic candidate, versus 2.08-2.09 s (0.30-0.31 s system time)
  on the older normal image. These are different kernel images/configurations,
  not a controlled patch-only comparison. The coherent-buffer copy is a
  plausible contributor; the measurements must not be presented as proof
  of unchanged performance or as a precise patch-attributed regression.

The review used the Sashiko snapshot
`d048f8742f6740c38833adf02e87901412d771ca`: general kernel review, technical
patterns, callstack and false-positive guidance, plus locking, allocation,
I/O-accessor, alignment and build guidance. There is no dedicated NAND
prompt in that snapshot. Actual callers, IRQ serialization, reset callbacks,
DMA allocation/unwind and removal paths were checked. The remaining hardware
contracts and performance question above are explicit review limitations;
passing checks do not imply Sashiko approval.

## Reproduce without hardware

```sh
python3 run_hardware.py
python3 run_suite.py --module NandPrivateRecovery
python3 run_lowlevel.py --candidate-source /path/to/sunxi_nand.c --expect-count-fixed
python3 run_suite.py --candidate-source /path/to/sunxi_nand.c --workers 4
```

See [hardware instructions](../hardware/README.md#actual-driver-abort-recovery)
for the separately authorized, destructive setup and restoration procedure.
