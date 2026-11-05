# Reviewable v10 and command-local tail decoding

This records the earlier 24-patch snapshot. The subsequent
[H616 abort-recovery update](H616-ABORT-RECOVERY.md) adds patches 20-21 and
renumbers the optimization/binding/format tail to 22-26. Its physical results
supersede the unresolved implementation status below; this record and its
validation limits are preserved.

The revised draft ends at `8a14d3e441eb` and keeps the NAND base
`30daaf3e7897b4a8d9b72e5b0c86dd6e2a4d7d85`. Its driver SHA-256 is:

```text
932f8d879868b5e80fd2441b8873a4c79d4182dc239b99e72c8937b53e996aaa
```

## Patch boundaries

| Patches | Review question |
| --- | --- |
| 1-3: shared OOB paths, command sizing, duplicate selection removal | Are the existing format, I/O ordering and error semantics preserved? |
| 4-19: correctness fixes | Does each triggering condition produce the described failure, and does its fix preserve successful transfers? |
| 20-22: register/cursor optimizations | Are results unchanged, with caches restricted to one completed transfer and logical indices kept separate from hardware slots? |
| 23: binding | Does the opt-in property describe the installed flash format? |
| 24: Allwinner format | Are geometry, markers, classification and representations consistent with the firmware format without changing the default format? |

The fixes remain separate from both refactors and optimizations. In particular,
pattern correction precedes status caching, and page-error cleanup precedes
DMA batching. Duplicate selection removal precedes clock-error propagation;
the DMA writer retains selection before its early register accesses. The
first 22 patches do not require the format extension. Historical sent revision
notes are preserved verbatim; validation artifacts stay outside kernel patches.

Only one small helper was added in this pass: `sunxi_nfc_buf_chunk_size()`.
The preparatory patch shares the existing SRAM limit. The later byte-counter
fix adds the command limit there. The format patch uses the same helper when
decoding the unprotected tail, so the writer and reader cannot drift to
different command boundaries. No additional callback layer or recovery policy
is introduced for this correction.

## Tail correction

The [previous reproducer](RECOVERY-COMPOSITION.md) showed that a NORMAL write
restarts the randomizer at every command, whereas the software decoder treated
the entire tail as one continuous stream. The correction restarts the software
stream at the original **tail write** boundaries. These are not necessarily
the raw-read boundaries: an unrandomized read may combine parity and tail.

The writer and stored bytes are unchanged. Decoding a tail no longer than one
command is unchanged too. On the admitted synthetic 2048+2048 geometry, the
1928-byte tail is decoded as 1024 and 904 bytes with the same initial seed for
each. All four PIO/DMA writer-reader combinations are checked. The tested
2048+128 physical part has an eight-byte vendor tail; this fix does not change
its format. No physical large-OOB part is identified by the synthetic tests.

The correction is folded into patch 24, where the software decoder is first
introduced, rather than submitting a knowingly broken intermediate feature.

## Stronger source and model checks

`run_measured_requests.py --expect-tail-fixed` requires exact vendor-tail
decoding. The full candidate suite uses this flag; it cannot accept the old
counterexample as a passing candidate. Running it on the prior driver fails
specifically at the independent `PLACE tail` comparison after compilation and
the C checks succeed. The pinned v9 checks remain separate and unchanged.

The candidate matrix covers these page/OOB byte counts:

```text
2048/128, 2048/256, 2048/2048, 2048/2049,
4096/512, 8192/1024, 16384/2048, 32768/4096
```

Actual core, page, DMA, command and register-helper bodies are extracted
unchanged. The hardware fixture decodes programmed USER_DATA_LEN fields and
enforces the 128-byte register bank independently of the logical-layout
helper. An independent oracle checks the first DMA batch and each subsequent
singleton batch. Larger pages exercise the 32-step limit and all four packed
length registers.

Every later standard-format DMA batch gets preparation and wait failures,
separately for reads and writes. Reads must deliver the whole page through PIO
retry and count corrected bits exactly once, preserving the prior statistics.
Writes must return an error without confirming or replaying a partly staged
program, and a subsequent independent write must succeed. These checks do not
assert that real failed DMA always retires safely; that remains a separate
hardware/provider contract.

`NandMeasuredPage` now checks software write-boundary decoding independently
of read chunking. Its 90 added positive cases include empty, short, exact,
multi-command and odd tails, three read chunk sizes and read/write failures.
Three negative controls require failures for continuous decoding, starting
from the raw-column origin, and using the wrong command limit. The scaled
model checks phase identities; the source fixture checks the concrete LFSR,
seed tables, byte counts and stored bytes.

`NandRecoverySequence` now separates `StopRetires` from `BusRetires`. Sixteen
additional policy cases vary both guarantees across PIO, external DMA,
register MDMA and descriptor MDMA. A reset can rescue an ineffective stop only
under an explicit retirement contract; if neither retires the producer and
bus accesses, the hypothetical policy must retain ownership. This is not a
new reset implementation, nor a claim that the observed bus-reset experiment
establishes a universal DMAengine guarantee.

## Evidence limits

The post-abort first-read data-placement anomaly remains unresolved. The
native scrambled odd-tail hardware read quirk and retained-MTD/unbind lifetime
issue also remain separately documented. TLA+ checks are exhaustive only for
their stated finite configurations and assumptions. Extracted-C coverage is
host-fixture coverage, not full kernel, SMP, silicon or unbounded refinement
proof. This extension performs no new board or flash operations.

The review used Sashiko's technical-patterns, callstack, false-positive,
locking, I/O-accessor, alignment, build, DT-binding and OF guides, plus upstream
`kernel/review-core.md`. The local prompt snapshot is
`d048f8742f6740c38833adf02e87901412d771ca`; it has no dedicated NAND guide.
Reviewing these prompts does not establish Sashiko approval. Exact run results
are recorded in [VALIDATION.md](../VALIDATION.md).
