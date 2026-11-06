# Generation-specific Allwinner OOB geometry

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
