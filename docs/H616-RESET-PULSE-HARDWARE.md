# H616 CCU reset-pulse hardware check

This is a fresh read-only test of the 26-patch v10 draft after replacing the
consumer's explicit 2 microsecond reset sequence with `reset_control_reset()`.
It does not relabel the earlier write-abort or PIO-timeout measurements.

The submission tip is `522614916748`, with recovery in `7eab915356ab`.
The hardware integration tip is `3282acb27959`, with recovery in
`8696449e2592`. Both have the same NAND driver source SHA-256:

```text
3d5176b4266d14a79782e6bbcb3e7cfea39ff347b6772db85112f600c4f103f7
```

The compiled CCU provider's source SHA-256 is:

```text
c895dfdaf2a1af2b5aeb28cc43070f40304b51b49f71f35223274e3bf778fef6
```

Its reset callback asserts the reset, calls `udelay(10)`, and deasserts it.
The 10 microsecond interval is identified from that implementation; this
test does not measure the reset signal electrically.

## Procedure and results

The H616 board booted a diagnostic integration kernel into RAM. UBI was
detached, no flash filesystem was mounted, and the module's write permission
remained disabled. Fresh raw data-plus-OOB snapshots of all four SPL copies
were checked against independent 512- and 1024-byte command reads. The bounded
UBI eraseblock used for the native-read comparison was also backed up.
No flash erase or program operation was part of this experiment.

The existing operation-29 bridge called the actual page-read callbacks.
Its one-shot hook returned `-ETIMEDOUT` after the real MDMA launch, at delays
of 0, 1, 2, 5, 10, 20, 50 and 1000 microseconds. Production abort, reset,
DMA cleanup and whole-page PIO retry ran unchanged. Both DMA modes used
the actual caller's streaming-mapped buffer; neither substituted private
coherent data storage.

| Check | Result |
|---|---|
| Abort/retry trials | 128: eight repeats, eight delays, programmed and erased pages |
| Controller busy at injected fault | 108 trials |
| Read callback result and full data/OOB comparison | Success and exact match in all trials |
| Baseline/final controls and subsequent DMA reads | 152 passed |
| Failed ECC during the trials | Zero |
| DMA prepare/cleanup counts, actual caller buffer and memory guards | All passed |
| Buffer stability immediately, after 1–2 ms, and through about 100 ms | All passed |
| Final raw snapshots | All SPL copies and the bounded UBI block unchanged |
| Ordinary MTD reads after unloading the probe | Three 32 MiB reads, each 2.06–2.08 seconds |
| Cleanup | Probe unloaded, source hooks removed exactly, plain Image rebuilt, normal boot restored |

The PIO retry inside the failing DMA callback is the first post-reset read:
there is no discarded read or extra diagnostic reset before comparing it.
The separate DMA follow-up occurs after the bridge's buffer-stability
observation interval. PIO and DMA parity bytes retain their existing distinct
representations; the independent bit-level oracle checks that distinction.

The three ordinary MTD reads ran on the diagnostic kernel with the module
unloaded and fault injection inactive. They are not presented as boots of
the subsequently rebuilt uninstrumented Image. The unchanged normal boot
image was restored afterwards and UBI attached successfully.

## Evidence and limits

The [capture](../hardware/h616-reset-pulse-20261105.json) contains controller
observations, comparisons and final cleanup checks, not flash contents,
flash-content hashes, target identities or private paths. Export pins both
source digests. Offline replay checks the full matrix and rejects incomplete
or unsuccessful records; it does not rerun hardware:

```sh
python3 -m hardware.reset_pulse_evidence
python3 -m unittest hardware.test_reset_pulse_evidence
python3 run_hardware.py
```

Twenty-one negative evidence mutations exercise wrong source/provider/API
identity, missing trials, incorrect data, failed ECC, wrong DMA ownership,
late buffer changes, missing active-engine observations and cleanup failures.
The earlier captures and their checkers retain their original source identity.

All 16 source-regression jobs pass for this source, and all 26 streaming
recovery model expectations match. The latter still include the deliberate
failed-reset/active-DMA unsafe-unmap counterexample. The Sashiko general,
technical, false-positive, subjective, locking, allocation, alignment and
MMIO guidance was checked against the affected recovery path and provider;
there is no dedicated NAND prompt. No new driver regression was identified.

This is bounded evidence for successful H616 CCU reset recovery, not proof
of arbitrary failed-reset retirement, permanently stuck hardware, every
bus/cache interleaving, older silicon or PREEMPT_RT execution. It does not
repeat pre-confirm write-abort or PIO-timeout matrices for the new pulse.
Those earlier results retain their explicit 2 microsecond implementation
identity in the [request-fault report](H616-REQUEST-FAULTS-20261105.md).
