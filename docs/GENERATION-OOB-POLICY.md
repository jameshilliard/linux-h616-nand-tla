# Generation-specific Allwinner OOB geometry

## Current draft: keep related format operations together

The 26-patch v11 draft ends at `9e7ee35abbbb`, with driver SHA-256
`1562e6617a6860753d5d836ccc9e9bfb19647f3aebbe92da01e35122002275cd`.
The opening refactor names the symmetric marker callback `compensate_bbm`:
it compensates hardware randomization on reads and writes so the ordinary
format retains a plain physical marker. `use_randomizer` still controls
the overall transfer format; these are separate responsibilities.

The final format patch now puts the generation-specific `spare_is_erased`
predicate in the selected OOB operations alongside its geometry. A10/A23
and H6/H616 keep their exact previous predicates. The controller selects
one const table, and the shared read-completion path uses that table's
predicate instead of selecting it separately from controller capabilities.
The ordinary tables do not install this firmware-format check.

This keeps six small, explicit tables and shared geometry/read helpers.
There is no nested operations hierarchy or runtime copying of callbacks.
Transport functions retain command sequencing, DMA ownership and cleanup.
The first 25 source prefixes are identical after normalizing the marker
callback rename and its explanatory comment; the predicate move belongs
only to patch 26. Historical cover notes and all sign-offs are preserved.

### Checks for the policy cleanup

- All 19 source/capture-replay jobs pass. The generation matrix checks the
  actual controller assignments and that each vendor table supplies its
  own predicate, with the obsolete fixture capability callback left NULL.
- The opening patch independently passes 768 marker cases and 1,227
  lifecycle cases. The final marker matrix passes 1,024 cases, including
  existing-image reads and new writes after late manufacturer setup.
- All 154 initialization-model expectations and all 119 OOB-pipeline
  expectations match. The latter replay 430 source-generated traces.
  Existing deliberate mutation and assumption counterexamples remain
  expected outcomes, not claims of universal correctness.
- Both ARM32 LLVM and ARM64 GCC `W=1` final-tree object builds pass. All
  26 b4 checks and strict checks for the two changed patches pass.
- The pinned historical C/marker checks and 13 Python unit tests pass.
  Historical late-manufacturer marker failures remain negative controls.

The fixture update replaced empty operations tables in direct vendor-helper
tests with actual selected tables. The initial ASan failure identified that
fixture mismatch, not a driver callback reachable with the ordinary policy.
Randomizer-only coverage lists erased-spare predicates as dependencies;
their behavior is exercised by the separate C and OOB-pipeline matrices.
Suite input tracking ignores generated Python cache files while continuing
to hash source, models and reference inputs.

Review checked Sashiko's technical-pattern, callstack, false-positive and
subjective guidance against the changed paths. No new driver defect was
identified in this cleanup. The separate reset-binding ABI review remains
open; these results do not establish send-readiness. No board deployment or
new hardware measurement was performed, and earlier evidence keeps its
original source identity.

## Previous draft: establish generation selection in the first refactor

The 26-patch v11 draft ends at `18524bbd6021`, with driver SHA-256
`02a11014ed40efaa47b77f7e383644b8a6036f910914a61e6357f5dbac48161a`.
The opening `centralize OOB handling` patch, `5b72dc03d9c3`, now introduces
controller-selected ordinary operations for A10/A23 and H6/H616. Each
generation has plain and scrambled-with-plain-BBM tables, with shared read
and completion callbacks. The fixed and variable geometry functions no
longer branch on the register layout to choose a generation.

The final patch adds the two Allwinner-format tables to that structure.
Packed firmware geometry and firmware-specific erased-page interpretation
remain with the format feature, so the opening refactor has no dependency on
later feature callbacks or the new binding. The first 24 patches continue to
apply independently of the format extension. Patches 2-25 preserve their
added/deleted lines and commit messages; historical cover notes are unchanged.

| Stage | A10/A23 | H6/H616 |
| --- | --- | --- |
| Opening refactor, ordinary plain or scrambled format | Fixed four bytes per step; preserve the extra two-byte ECC-strength reservation | Reserve the first word when choosing ECC strength; maximize variable user-data lengths afterward |
| Final Allwinner-format feature | Fixed four bytes per step, randomized marker | Pack `min(4 * steps, 16)` bytes in step zero, randomized marker |

### Checks for this draft

- All 19 source/capture-replay jobs pass. The direct generation matrix now
  covers 274 ordinary/vendor geometry and error cases, including the actual
  A10, A23 and H616 caps-table assignments. Existing geometry totals remain
  4,872 accepted, 11,200 rejected and 56 ordinary core-fallback cases.
- The opening patch independently passes 768 marker cases and 1,227
  lifecycle cases. Its four ordinary geometry helper bodies are byte-for-byte
  identical to the functions executed by the final-tree geometry matrix.
  The zero-step older-controller fallback and H6 allocation failures retain
  their existing behavior.
- The initialization model now distinguishes all six generation/format
  policies. All 154 expectations match: 128 positive cases, 24 deliberate
  mutation counterexamples and two explicit late-SLC assumption
  counterexamples. Wrong-generation selection is checked for ordinary as
  well as vendor formats.
- The unchanged format callbacks pass 430 source OOB-pipeline traces and
  all 119 associated TLC expectations (108 observed trace projections and
  11 deliberate mutation counterexamples).
- ARM32 LLVM and ARM64 GCC `W=1` object builds pass for the opening patch
  and final tree. All 26 b4 checks pass; strict checkpatch reports no errors,
  warnings or checks for either changed patch. Export preserves all patch
  sign-offs and historical cover-letter changelogs.
- Review used Sashiko's general technical-pattern, callstack and
  false-positive guidance, together with OF and allocation guidance. The
  prompt snapshot has no MTD-specific checklist. No new confirmed driver
  defect was identified in this rearrangement.

This is a submission refactor and host/model validation, not a new board
deployment. Historical hardware observations keep their own source hashes.
The marker-only fixture stubs geometry; the full geometry runner executes
the actual functions. Existing model/hardware limits below still apply.

## Previous draft: generation split confined to the final patch

The v11 draft at `60b1bc96cc0b` separates the Allwinner format's geometry
callbacks by controller generation. Its driver SHA-256 is
`6e2f9e7d696a6be37f0ad40a6787d8aaf31e526bfe63ed2bff051819108e8e4d`.
The change is folded into the final format-support patch. Patches 1-25 and
the historical cover-letter changelogs are unchanged.

## Policy and layout

| Controller capabilities | Vendor geometry | Shared operations |
| --- | --- | --- |
| A10 and A23 | Ordinary minimum-OOB reservation helper; four protected user-data bytes per ECC step; reject zero or negative step counts | Randomizer enabled, no BBM compensation, common read classification and completion |
| H6/H616 register generation | Reserve `min(4 * steps, 16)` bytes, all in step zero; later steps have parity only; reject zero or negative step counts | The same randomizer policy, read classification and completion |

The caps table selects the vendor ops table. Attach uses a local lookup for
geometry, and final installation still occurs once after manufacturer
initialization and before BBT reads. The two ordinary format tables remain
unchanged. The existing generation-specific erased-spare predicate remains a
separate capability callback.

## Executable checks

The source extractor obtains each of the A10, A23 and H616 capability-table
assignments from the actual driver. Tests compare those assignments against
the expected generation independently of the selector being tested.

- 140 direct geometry/error cases check fixed versus packed allocation,
  unchanged reservations, invalid step counts and H6 allocation failure.
  Additional assertions check property precedence, a side-effect-free lookup,
  and shared read/completion callbacks.
- The wider geometry matrix still accepts 4,872 cases, rejects 11,200 cases
  and preserves 56 ordinary-format core-fallback cases. Its rejected set
  includes 3,600 geometries above the controller's ECC-step limit.
- All 19 source/capture-replay jobs pass for the exact driver digest above,
  including the source-executed marker, lifecycle, byte-format, codec and
  transport checks. The pinned older source also remains supported.
- `NandDeferredInitialization` distinguishes fixed, variable and packed
  geometry and both vendor policies. All 152 expectations match: 128 valid
  configurations, 22 deliberate mutation counterexamples and two explicit
  late-SLC assumption counterexamples. The new wrong-generation mutations
  must violate `GenerationMatches` for both controller generations.
- The byte-level OOB pipeline passes all 119 TLC expectations: 108 observed
  source-trace projections and 11 deliberate checker-sensitivity controls.
- ARM32 LLVM and ARM64 GCC driver-object builds pass with `W=1`. All 26 b4
  patch checks and strict checkpatch for the changed patch pass.

```sh
python3 run_suite.py --source-only --candidate-source /path/to/sunxi_nand.c
python3 run_suite.py --module NandDeferredInitialization
python3 run_oob_pipeline.py --candidate-source /path/to/sunxi_nand.c
```

## Boundaries

This is a policy/geometry refactor, not new hardware evidence. Stored hardware
captures retain their original source identity. Host allocation faults and
finite model states do not prove arbitrary kernel execution or older-silicon
behavior. The marker-only fixture stubs geometry callbacks; the full geometry
runner separately executes their actual bodies. The existing hypothetical
late-SLC/subpage limitation remains explicit rather than being counted as a
passing configuration. No new runtime defect was found in this split.
