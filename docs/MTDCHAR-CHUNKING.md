# MTD character-device page/OOB chunking

## Review correction: complete the OOB tail

The four-patch v2 draft adds `6c4f993ed125` to the posted three-patch v1.
It continues after the data stream finishes, switches remaining operations
to OOB-only mode, advances beyond a final partial data page, and limits
OOB-only writes to one page for raw NAND. The pinned correction lives in
`reference/mtdchar-chunks-v2/`; its digest is checked before applying it to
the unchanged v1 function snapshot in a temporary directory.

Run `python3 run_mtdchar.py --v2` to reproduce the new checks. The source
bodies reconstructed this way match the integrated candidate exactly.
The v2 command is self-contained for CI; the default command and three-stage
runner retain the earlier version as a control. The model's `CompleteOob`
parameter distinguishes the two algorithms explicitly.

The v2 matrix passes 5,771 transfers, including 300 short-data/long-OOB
cases, with no mismatch. Four focused v1 cases reproduce truncated OOB
completion. All 17 bounded protocol expectations and nine source-trace
expectations pass. The allocation, user-copy, transport and ECC checks,
24 partial failures, 18 request edges, 81 ECC sequences and 1,375,584
arithmetic cases also pass. Six harness unit tests pass. These are bounded
host/model results, not physical NAND or unrestricted C proofs.

The corrected revision is not yet posted to the mailing list and is not
declared as a published prerequisite. The historical results below retain
their original source identifiers, bounds and counts.

## Historical three-patch validation

This extension validates the independent mtdchar page/OOB chunking fixes,
not the Boot0 mapper or the sunxi controller implementation. The current
submission draft is a three-patch v1 series. The source snapshot includes
the final patch's readability cleanups:

- Base: `d48723711bae9b8cdc56719c448ba42fef28cbee`.
- Local candidate: `35230cb6d8b9bbdca23abd90ad7177077ec03ea9`.
- Combined stable patch ID: `462a78300fb1dd998dc6fe444527295de712a45c`.
- Exact extracted source and hashes: `reference/mtdchar-chunks/`.

The original unsplit candidate was `51e1737afbf8d2e6902814add5848c1522f36973`
(patch ID `31e5910c9c1ca598ec8872fed0cfffcc579bf867`). Its snapshot remains
in repository history. The revised source uses the kernel's arbitrary-multiple
`rounddown()` helper, names the remaining OOB length explicitly, and corrects
the page-boundary comments. The C fixture supplies the same `rounddown()`
expression as `include/linux/math.h`; the TLA+ transition system is unchanged.

The candidate is not yet a public prerequisite. Source snapshots are included
so these checks do not depend on a developer's checkout or unpublished Git
objects. `--candidate-source` can also check a full current `mtdchar.c` file.

## Run

```sh
python3 tests/test_mtdchar.py
python3 run_mtdchar_model.py --download
python3 run_mtdchar.py
python3 run_mtdchar_admission.py
python3 run_mtdchar_admission.py --candidate-source path/to/mtdchar.c
python3 run_mtdchar_series.py
python3 run_mtdchar_series.py --sources patch1.c patch2.c patch3.c
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
call traces. The admission runner exercises the real MTD admission wrapper and
a separate statistics model. The series runner checks intermediate sources
with the same C oracle. None of these commands accesses hardware.

Source-trace checks partition each geometry into batches of at most 8,192
steps (recorded calls plus initialization and completion). A single longer
trace stays intact in its own batch. This keeps the generated TLC inputs
within the runner's 1-GiB heap for the current matrix, including long OOB-only
tails. Every input trace, invariant and termination check is retained; the
batches do not shorten executions or sample the request domain. Unit tests
check exact input preservation, batch boundaries and the negative controls.
The model keeps its selected trial unchanged throughout an execution, so
there are no cross-trial transitions lost by checking batches separately.

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
| `NandMtdCharStats` and actual admission wrapper | A core error before initializing request statistics | Pre-existing bug reproduced in baseline and chunking candidate; the separate initializer fix passes both alone and combined with chunking |

All protocol actions listed as required were reached. Expected failures must
be the named invariant with a real TLC counterexample: parser errors, crashes
and timeouts are failures of the validation, not accepted negative tests.
Line/branch coverage is a reachability measurement, not a correctness proof.

## Three-patch submission split

The split is based on MTD `mtd/fixes` at
`1c1a342aceec79528b3ba51f376eca2be5928fe7`. Its baseline `mtdchar.c` matches
the original mainline base. The patches are:

1. `3096e84b0e57` — calculate OOB capacity from the starting column.
   Patch ID: `07a4ff0417eef2d0ae148dee29c03bf02261775f`.
2. `18cbeff590d0` — bound OOB reads to the data in intermediate chunks.
   Patch ID: `343a650cc574481d4117448117e363f35ba6cd0d`.
3. `35230cb6d8b9` — keep OOB chunks aligned with their data pages.
   Patch ID: `6912b6051bb6e00997e1d107ef0e86f7e4f19198`.

These are local submission commit identifiers, not posted prerequisites.
The final source snapshot includes the readability cleanups; the baseline
and core snapshots retain their original source revisions. The series runner
reconstructs the first stage by replacing only `adjust_oob_length()`, then
the second by inserting the intermediate MEMREAD cap. The final stage is
the pinned candidate. With `--sources`, it verifies every extracted function
against those stages before checking the actual supplied sources.

The same 5,471 requests are executed at each stage. The runner rejects any
regression in a previously passing request and verifies identical ordinary
request behavior across the preparatory patch:

| Stage | Newly passing requests | Remaining mismatches |
|---|---:|---:|
| Base | — | 1,352 |
| Capacity refactor | 0 | 1,352 |
| Intermediate-read cap | 183 | 1,169 |
| Complete page-aligned chunking | 1,169 | 0 |

All four stages pass ASan/UBSan and emit identical sanitizer/gcov call traces.
Each kernel commit builds `drivers/mtd/mtdchar.o` with `W=1` on x86-64.
The helper arithmetic and fault tests cover the revised final source. This is
bounded regression evidence for the intermediate stages; only the complete
final implementation is checked against the full corrected TLA+ protocol.

The capacity patch is preparatory and has no `Fixes` or stable trailer. Its
synthetic wide-product check is not presented as evidence of a reachable
large-device bug. The two behavior fixes carry their introducing commits
and stable Cc. The independently submitted MEMREAD statistics initializer
is not included in this series and remains a separate known negative control.

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

A second test admits the first chunk, then rejects a later chunk. With
4-byte pages, 5-byte OOB, a 16-byte buffer limit and a 24-byte device, a
20-byte-data/31-byte-OOB request completes one chunk with three corrected
bitflips before core OOB validation rejects the next chunk. Baseline and
candidate both accumulate undefined counters on that rejection as well.

The initial fixture-only change to `struct mtd_req_stats stats = {};` returns
zero statistics for a rejected first operation and preserves the valid prefix's
statistics when a later operation is rejected. The declaration is inside the
loop, so this initializes each iteration. The model retains the original
defect as an expected counterexample instead of weakening the property.

The same one-line fix is now prepared as the independent kernel patch
`mtdchar: initialize MEMREAD statistics before each read`:

- Local commit: `8228da316bffce30376986cca21833e7db1d010b`.
- Stable patch ID: `bcb169861b26335771b9f8ba053bd42acf588645`.
- Introducing commit: `095bb6e44eb1` (`mtdchar: add MEMREAD ioctl`).
- Base: `d48723711bae9b8cdc56719c448ba42fef28cbee`, without the chunking patch.

The admission runner's `--candidate-source` option extracts the supplied
source without modifying its initializer. Both the actual standalone fix and
its combination with chunking pass the first- and later-rejection tests.
Applying the two patches in either order produces identical Git trees. The
combined source also passes the chunking and ECC-aggregation C regressions.
The baseline and core snapshots and their expected failures remain unchanged.
The statistics fix was subsequently rebased onto MTD `mtd/fixes` at
`1c1a342aceec79528b3ba51f376eca2be5928fe7` and submitted separately as local
commit `558159d117c03b258a675e37ab54e9dc649f4040`, with the same patch ID.
The chunking series has no prerequisite on that fix.

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
