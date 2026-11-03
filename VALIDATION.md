# Initial validation record

Validated on 2026-11-03 against the posted 18-patch NAND OOB v9 implementation.
The locally inspected v10 draft had identical driver/binding contents. Reference
digests and the exact sent commit are in [source.json](source.json).

## TLC

TLC 1.8.0, one worker, seed 1, fingerprint polynomial 0, explicit-state checking.
All 68 configurations matched their expectations:

- 48 positive configurations: invariants and termination properties passed.
- 19 mutation configurations: each produced its expected invariant violation.
- 1 assumption configuration: ineffective DMA abort produced `NoUnsafeUnmap`.
- All required positive-model actions were reached; no uncovered required action.

Positive configurations generated 124,384 states and found 113,822 distinct
states, summed across separate configurations. Including negative tests, the
corresponding sums were 133,578 and 122,659. These are finite-instance counts,
not a proof for unbounded geometries or executions.

## Source-extracted C

56 unchanged function bodies from the pinned driver compiled with GCC 15.2.0,
`-Wall -Wextra -Werror`, AddressSanitizer, UndefinedBehaviorSanitizer, and the
kernel-style overflow/aliasing flags recorded by `run_c.py`.

| Check | Result |
| --- | --- |
| Layout, user-data allocation, DMA batching and packed length codes | 614,118 admitted layout cases passed |
| Randomizer | All 65,536 input seed values at advances 0/1/8; both full ECC seed tables; byte transforms and BBM symmetry passed |
| Firmware erased-spare predicates | 921,600 truth-table/boundary cases passed |
| Generic ECC patterns/errors | 6,912 combinations passed |
| Vendor ECC classification and decoded OOB | 3,456 combinations passed |
| Unprotected OOB coalescing, representation and transfer faults | 612 cases passed |
| Page/subpage/OOB writes, single-fault injection | 412 executions passed |
| Page/subpage/OOB reads, single-fault injection | 456 executions passed; both ECC counters rolled back on failed DMA reads |
| ECC initialization | 4,872 hardware-ECC configurations accepted, 11,200 rejected, 56 left for core software fallback; all assertions passed |
| Over-limit geometries within that initialization matrix | 3,600 rejected |
| Additional boundary checks | Small-page column commands, tail cursor, raw-reread errors, zero-valued pattern-ID cache, counter-word reuse, 16/32-step bitmap boundaries passed |

No sanitizer failures or compiler diagnostics remained in the final run.
Local tools were Python 3.14.4 and OpenJDK 25.0.4.1. The provided CI template
targets Ubuntu 24.04 with Java 21, but hosted CI has not run: activating the
workflow requires a workflow-authorized publishing credential.

## Scope of this result

No kernel implementation was changed by this validation, and no new hardware
test was performed. The unresolved successful-DMA-stop assumption, media
classification limitations, core contracts, and other coverage boundaries are
listed in [docs/ASSUMPTIONS.md](docs/ASSUMPTIONS.md).

Rerun the scripts when changing the reference or models (or use CI once it is
enabled). The runners record source/model hashes in their machine-readable
summaries; this document records the initial checked version, not future runs.
