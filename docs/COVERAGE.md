# Coverage of the submitted v9 series

The exact driver is [reference/sunxi_nand.c](../reference/sunxi_nand.c).
`run_c.py`, `run_lowlevel.py` and `run_lifecycle.py` list every extracted driver
function; the summaries record their hashes. Broader core, provider and codec
checks are mapped in [INTEGRATION-COVERAGE.md](INTEGRATION-COVERAGE.md) and
[DEEP-VALIDATION.md](DEEP-VALIDATION.md).
The [refinement map](REFINEMENT-VALIDATION.md) adds connected command transport,
actual manufacturer retry callbacks, MTD lookup/release and byte ownership.
The [probe trace map](PROBE-TRACE-CONFORMANCE.md) additionally connects
observed scan/list/registration/unwind events to a separate TLA+ protocol.
The table distinguishes modeled behavior from directly executed driver bodies.
It does not imply full kernel line/branch coverage.

The [recovery-composition map](RECOVERY-COMPOSITION.md) adds 149 model
configurations and 256 candidate-source core calls for the five remaining
recovery/placement/measured-stream/write/compound-failure areas. It also
records their physical evidence boundaries and new conditional tail findings.

The independent [mtdchar chunking map](MTDCHAR-CHUNKING.md) adds bounded
MEMREAD/MEMWRITE protocol models, actual ioctl trace checks, allocation and
partial-I/O failures, and wide page/OOB arithmetic. Its baseline and candidate
are pinned separately from this historical sunxi v9 coverage record.

| Patch | Critical behavior | Validation |
| --- | --- | --- |
| 1, centralize OOB handling | Protected/parity/free-region offsets; first two bytes reserved for BBM; per-format operations | `NandBatch`, `NandOob`; C layout functions and generic/vendor batch/finish functions |
| 2, reject excessive ECC steps | 16-step legacy and 32-step H616 limits | C executes `sunxi_nand_hw_ecc_ctrl_init()` across accepted/rejected geometries; `NandBatch` checks admitted layouts at both limits |
| 3, PIO OOB length | Logical step determines the length even though PIO uses hardware slot zero | `NandBatch` and its wrong-slot mutation; C packed-length helpers and extracted PIO read/write chunks |
| 4, page setup / erased-check errors | Negative transport errors propagate; raw ECC failure differs from transport failure | `NandTransfer`, `NandEcc`; C page/subpage setup, raw-reread failures, and whole-read fallback |
| 5, completion reuse | Register updates precede completion; timeout drains IRQ before reuse; recheck excludes already acknowledged events | `NandIrq` and composed `NandController`; actual interrupt/wait/reset functions run in low-level fixtures, including partial completion and W1C preservation |
| 6, failed programs | Stop on error; disable ECC/randomizer; no program confirm after an earlier failure | `NandTransfer`; C executes page/subpage/DMA write control flow with a fault at each external transport call |
| 7, per-step pattern ID | Select the current hardware-slot bit, not the entire pattern word | `NandEcc`; all six pattern/error states across three logical steps in C |
| 8, synthesized patterns | Zero/ones fills survive later processing; only ones count as erased | `NandEcc`; C generic batch checks data, OOB, counters and guard bytes |
| 9, buffer/column errors | Raw data/OOB errors and column-position failures unwind properly | C read/write chunks, tails, unprotected reads and page wrappers; `NandTransfer` |
| 10, OOB program confirm | OOB wrapper delegates to the page writer without a second confirm | C OOB writes through both PIO/DMA page callbacks; duplicate-confirm mutation |
| 11, small-page repositioning | Use a fresh page read, not unsupported random-column commands | C `sunxi_nfc_read_column()` and ECC initialization; small-page PIO batch configuration |
| 12, DMA batching | Limit the packed user-data bank; later variable-length batches use one step; restore the spare base after column commands | `NandBatch` plus three mutations; C batch/length helpers and extracted multi-batch page control flow |
| 13, ECC snapshots | Pattern ID zero is valid; counters share a word for four slots; each transfer invalidates the cache | `NandEcc`; C snapshot tests and two-pass generic error processing |
| 14, extra OOB cursor | Skip only a redundant column change; update the cursor after a successful tail transfer | C extra-OOB read/write functions and cursor/fault tests |
| 15, chip selection | Page commands perform selection; DMA write prepares its initial controller state explicitly | Extracted page control flow; low-level harness executes chip selection, `exec_op()`, its actual core parser, and suboperation register programming; `NandCommand`/`NandLifecycle` |
| 16, length-register access | Pack eight 4-bit codes per word; clear unused slots | C exact PIO/DMA packing/reset helpers; `NandBatch` abstract code array (not MMIO access-count/performance proof) |
| 17, binding flag | Opt in to the randomized-OOB format | Exact binding bundled for context; schema validation / DT parsing are not part of this suite |
| 18, randomized OOB | Packed protected prefix; per-step randomizer restart; zero-page precedence; firmware erased-spare heuristic; decoded BBT data; unprotected parity/tail reads | `NandEcc`, `NandOob`, `NandMedia`, `NandCore`; C vendor attach/reads/finish, whole-page subread, predicate truth tables, metadata identity, re-encoding, coalescing and byte transforms |

## C checks and independent oracles

- Layout tests walk offsets independently, validate region bounds/BBM exclusion,
  and inspect every packed code, including unused hardware slots.
- ECC initialization runs the submitted function, including normalization to
  1 KiB ECC steps, supported-strength selection, OOB fit, controller limits,
  and PIO/DMA callback selection. The NAND core's small-page software fallback
  is an explicitly reviewed boundary, not an executed core implementation.
- Randomizer checks use a bit-by-bit shift register rather than the driver's
  closed-form step. They cover every 16-bit input to the 15-bit state function
  for advances 0, 1 and 8, both complete derived ECC seed tables, byte transforms,
  page-index wrapping, and BBM compensation symmetry.
- Firmware predicates are tested over all 1,024 erased/non-erased arrangements
  in ten bytes, with 15 representative non-`0xff` values and boundary pages.
  This validates the predicate, not whether that heuristic proves a page blank.
- ECC output checks use separate page-level expectations rather than calling
  the driver's classification helper twice. C covers 6,912 generic and 3,456
  vendor combinations, plus 16/32-step bitmap boundaries.
- Read/write fault injection executes unchanged page, subpage, and OOB wrapper
  functions. Transport/IRQ/DMA APIs are fixtures. These driver-path matrices
  inject at most one fault per run; a subsequent fallback is allowed to succeed.
  Both ECC statistics are tested for rollback after a failed DMA read. Separate
  core tests add compound retry/transport and BBT replacement failures.
- The low-level suite separately executes the actual IRQ, DMA backend,
  command-parser, SRAM, randomizer-control, chip-selection and timing functions.
  The harnesses do not link every layer together in one kernel execution.
- Distinct-byte media fixtures exercise raw PIO round trips, page/column
  identity and partial programs. Generic OOB finish checks include absent tails,
  optional/matching cursors, both representations and read failures. Separate
  metadata tests check protected bytes, BBM compensation and untouched sentinels.
- Actual attach policy covers engines, format selection, inherited ECC
  requirements and allocation failure. The core harness now executes actual
  request/retry/statistics and BBT bodies. The lifecycle harness adds actual
  scan/cleanup and timing negotiation while retaining the documented service
  and manufacturer boundaries.
- Independent BCH polynomial encoding, real decoding and sunxi-tools image
  construction supplement the original synthetic-media tests. Exact old Linux
  and BSP routines check legacy offsets, seed selection and metadata packing.
- Sanitizers check host memory accesses. They cannot validate real MMIO side
  effects, DMA coherency, or the kernel ABI from these simplified structures.

## Finite state spaces

`cases.py`, `extended_cases.py`, `integration_cases.py`, `validation_cases.py`,
`boundary_cases.py`, `next_cases.py`, `hardware_cases.py`, `contract_cases.py`,
`probe_cases.py` and `probe_trace_cases.py`
are the executable lists of bounds and assumptions. Layouts include
legacy 16-step pages, H616 2/32-step packed-prefix pages, a 32-step geometry
exceeding one user-data bank, uneven lengths, nonzero subpage starts, and a
512-byte small page. OOB models test both DMA and PIO representations and an
empty tail. IRQ models have one/two required events and two successive
operations. The transfer model has three batches. ECC enumeration normally has
three steps and six possible statuses per step; an additional case mixes raw
erased, uncorrectable and transport-error results independently. A six-step
case crosses the four-counter word boundary.

Every positive configuration checks its invariants and eventual completion
under the model's fairness assumptions. The runner also requires each listed
positive-model action to be reached across the full suite. This is model action
coverage, **not source-code branch coverage**. The extended seven-area mapping,
additional bounds and source GCOV accounting are in
[EXTENDED-COVERAGE.md](EXTENDED-COVERAGE.md).

## Review guidance used

Model scope and possible findings were checked using Sashiko's general
`review-core.md`, `kernel/technical-patterns.md`, `kernel/callstack.md`, and
`kernel/false-positive-guide.md`, plus the `locking`, `io-accessors`, `alignment`,
`mm-alloc`, `dt-bindings`, `of`, and `build` subsystem prompts. The local prompt
snapshot was `feed8a17375278783d7277a5f1eedc32fa3230f9`; general review guidance was
also read from the public repository. No dedicated MTD/NAND prompt was present
in that snapshot.

Guidance: [Sashiko review prompts](https://github.com/sashiko-dev/sashiko/tree/main/third_party/prompts).
This was a modeling/contract review, not a new kernel build, binding validation,
hardware test, or a guarantee of Sashiko approval. Outstanding assumptions are
listed separately in [ASSUMPTIONS.md](ASSUMPTIONS.md).
