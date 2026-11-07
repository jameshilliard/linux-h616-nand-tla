# MTD character-device page/OOB chunking

This extension validates the independent, unsent v1 patch
`mtdchar: keep data and OOB page offsets aligned across chunks`, not the
Boot0 mapper or the sunxi controller implementation. The kernel patch is
unchanged by this modeling work.

- Base: `d48723711bae9b8cdc56719c448ba42fef28cbee`.
- Local candidate: `51e1737afbf8d2e6902814add5848c1522f36973`.
- Stable patch ID: `31e5910c9c1ca598ec8872fed0cfffcc579bf867`.
- Exact extracted source and hashes: `reference/mtdchar-chunks/`.

The candidate is not yet a public prerequisite. Source snapshots are included
so these checks do not depend on a developer's checkout or unpublished Git
objects. `--candidate-source` can also check a full current `mtdchar.c` file.

## Run

```sh
python3 tests/test_mtdchar.py
python3 run_mtdchar_model.py --download
python3 run_mtdchar.py
python3 run_mtdchar_admission.py
```

These dedicated runners pin the official stable TLA+ Tools v1.7.4 release
(TLC 2.19), including its SHA-256 and source revision in `mtdchar_cases.py`.
The initial local checks also passed with the repository's cached November 3
v1.8.0 prerelease. That prerelease's download asset was replaced upstream, so
fresh downloads fail the historical checksum; it is not used by this workflow.
The older NAND suite's toolchain pin and historical records are unchanged.
Stable TLC records counterexample states in `tlc.log` rather than JSON trace
files. The harness still requires the named invariant and its actual trace.

The first runner exhaustively explores a finite protocol. The second executes
actual C functions under ASan/UBSan, compares bytes with independently indexed
media, then checks their recorded backend calls against TLA+. Its separate
non-sanitizer build records source-mapped gcov coverage and must emit identical
call traces. The last runner exercises the real MTD admission wrapper and a
separate statistics model. None of these commands accesses hardware.

The new workflow runs these checks in GitHub Actions. They are also included
in the appropriate full-suite runners; `run_suite.py`'s sunxi candidate argument
does not silently replace the separately pinned mtdchar candidate.

## Coverage and results

| Layer | What is checked | Result |
|---|---|---|
| `NandMtdCharChunks` | Allocation/unwind, every small request length and starting column, chunk planning, backend page iteration, data/OOB identity, no repeated pages, progress, copy/transport failures, ECC precedence | 8 positive configurations, 7 required mutation counterexamples, 2 explicit backend-assumption counterexamples |
| `NandMtdCharTrace` | Exact call address, requested/returned lengths, page/OOB identity, return counts and ordering from the C ioctl | All 5,471 request traces checked; baseline repeat and 3 tampered-trace controls detected |
| Extracted C | RAW/PLACE/AUTO capacity, normal and oversized OOB, unaligned reads, permissive subpage writes, OOB-only operation, error cleanup and wide arithmetic | 5,471 candidate transfers without mismatch; baseline has 1,352 mismatches |
| Extracted C arithmetic | Every column for widths 2 through 2,048, irregular OOB sizes, multiple eraseblock geometries, large absolute page indices | 1,375,584 cases, plus a page/OOB product exceeding `UINT32_MAX` |
| Extracted C errors | Allocation/user-copy failures, missing methods, EOF, overflow, NULL pointers, legacy length masking, partial backend failures, mixed ECC sequences | 24 partial-failure cases, 18 request-edge cases, 81 four-chunk ECC arrangements, plus the original error matrix |
| Host source coverage | The four changed mtdchar functions, excluding shims and backend fixtures | 159/159 executable lines and 102/102 branch edges reached |
| `NandMtdCharStats` and actual admission wrapper | A core error before initializing request statistics | Pre-existing bug reproduced in baseline and candidate; fixture-only initialization experiment passes |

All protocol actions listed as required were reached. Expected failures must
be the named invariant with a real TLC counterexample: parser errors, crashes
and timeouts are failures of the validation, not accepted negative tests.
Line/branch coverage is a reachability measurement, not a correctness proof.

## Chunking argument

Let `W` be data bytes per page, `O` the effective OOB bytes for the selected
mode, `E` the eraseblock buffer limit, and `c` the starting data column.
The modeled geometry requires `W > 0`, `E >= W`, `E % W == 0` and
`0 <= O <= oobsize`.

1. The OOB buffer is at least one page's OOB whenever more OOB remains. Thus
   rounding a non-final OOB chunk down to a multiple of `O` retains at least
   one complete page. An OOB request with `O == 0` is rejected first.
2. If that chunk holds `n` pages of OOB, the accompanying data is limited to
   `n*W-c`. This is positive because `n >= 1` and `0 <= c < W`.
3. Non-final data chunks end on a page boundary. Limiting their OOB to the
   data-covered pages prevents a symmetric backend from visiting later OOB
   pages ahead of the next data chunk.
4. Data-led requests advance by returned data bytes. OOB-only requests
   advance by completed OOB pages times `W`. A final partial OOB page does
   not need another iteration.
5. Returned byte identities are checked against the original request, not
   against the planner's own output. A shifted write followed by an equally
   shifted read cannot satisfy this oracle merely by round-tripping.

The relative page-count calculation avoids truncating absolute page indices.
The product uses `u64`; `size_t` allocations and the legacy 32-bit request
length mask are exercised by C rather than pretending TLC's bounded integer
domain proves all machine arithmetic.

## Backend contracts and ABI limits

Raw NAND combined reads stop when data ends. SPI-NAND's page iterator continues
until both streams end. The source fixtures preserve that distinction and
reject raw-NAND multi-page OOB-only writes, as the actual backend does. They
do not turn those writes into an invented supported operation.

A final SPI-NAND read may return more OOB pages than data pages. The patch
intentionally does not apply its intermediate-read OOB cap to that final call.
Requests with arbitrary excess OOB can still return a short OOB count under
the existing data-led ioctl loop; complete-delivery assertions apply to the
compatible data/OOB range, not to every conceivable pair of user lengths.

Fatal backend errors may have transferred a prefix internally. The ioctl
does not retry or commit that chunk's return lengths. Copy failures can expose
part of a userspace buffer without committing its counters; no transaction or
rollback guarantee is claimed. Corrected and uncorrectable ECC statuses may
carry data and continue; final error precedence must retain earlier failures.

The model deliberately breaks two backend assumptions: success with zero
progress, and successful partial OOB-page completion. They produce progress
and byte-identity counterexamples. No specific NAND backend is claimed to
violate these contracts; mtdchar is not proved safe against arbitrary malformed
provider returns.

The existing non-NULL-data-pointer/zero-length no-op and original-length versus
32-bit-mask accounting are recorded as compatibility behavior, not endorsed
as a new ABI design. The model begins after request normalization; the C edge
tests cover those input-normalization cases.

## Confirmed separate finding: rejected-read statistics

`mtdchar_read_ioctl()` declares an uninitialized `struct mtd_req_stats stats`
and accumulates its fields immediately after `mtd_read_oob()`, before checking
the return code. `mtd_read_oob()` can return from `mtd_check_oob_ops()` before
its later `memset(ops->stats, ...)`.

A concrete request reads one data byte in the final page, with OOB length one
byte beyond the remaining OOB capacity. The data bounds are valid, so it
reaches core OOB validation. Core returns `-EINVAL` without entering a device
backend; the ioctl nevertheless copies the undefined statistics to userspace.
This is independent of the new chunking helper and reproduces on the base.

The admission test compiles unchanged functions with GCC's
`-ftrivial-auto-var-init=pattern`. All three returned ECC counters become
`0xfefefefe`, making the missing initialization deterministic. This is evidence
of reading and exporting undefined statistics, not a measured stack disclosure
on a production kernel. Automatic-variable initialization configuration can
mask the symptom without repairing the source contract.

A fixture-only change to `struct mtd_req_stats stats = {};` returns zero
statistics for the rejected operation. It has **not** been folded into the
kernel submission. The model retains the current defect as an expected
counterexample instead of weakening the property to claim a clean result.

## Review and remaining limits

The validation used the Sashiko general technical, call-stack, pointer-guard
and false-positive guidance, plus the syscall, alignment, VFS and allocation
guides. Caller validation, error ordering and actual raw-NAND/SPI-NAND loops
were inspected. There is no MTD-specific prompt in the checked prompt set.
The statistics issue is a confirmed source/contract defect, not a style request.

This is bounded model checking and source conformance, not a machine-checked
unbounded proof of Linux. Physical NAND timing, randomization/ECC algorithms,
DMA, MTD unbind, arbitrary backend implementations, concurrent modification
of media/userspace and hardware write-granularity rules are outside this
extension. Those concerns are not made safe merely by the chunking fix.
