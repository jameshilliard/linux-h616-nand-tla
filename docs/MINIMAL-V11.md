# Minimal sunxi NAND v11 correction

## Source and scope

The submission draft at `82d7d88fc332d8641d90a32266b27e165e56d539`
has driver SHA-256
`6a4e8c05d61e02a09275a4b434203df33e0ea2be48b5c33d6454329e35f5e02b`.
It returns to the previously checked minimal correction, keeping the
26-patch organization and base of the sent v10.

The final tree differs from sent v10 only in `sunxi_nand.c`: eight added
and fourteen removed lines. Patch 1 uses a single ordinary-format ops
table and checks `NAND_NEED_SCRAMBLING` inside the marker transform.
Toshiba/Hynix manufacturer initialization can set that flag after attach;
the hardware randomizer and BBM compensation must see the same value.
The Allwinner-format ops still omit marker compensation deliberately.

Patches 2–26 retain their v10 added/deleted lines and commit messages.
Patch 26's patch ID changes because its context now contains the single
ordinary-table assignment, not because its implementation changed.
Historical cover notes are preserved verbatim; v11 notes describe the
late-initialization correction. All 26 patches retain the author's sign-off.

The larger policy experiment is preserved separately. The current draft
does not move BBT creation out of NAND core or add immutable randomizer
policy, a finalization phase, or six generation-specific ops tables.
Existing geometry branches and the generation-specific vendor spare
predicate remain where v10 put them.

## Executable validation

- All 19 source/capture-replay jobs pass against this exact driver.
- The final marker matrix checks 1,024 cases with no read or write
  mismatch. The standalone opening refactor passes 768 marker cases and
  1,213 lifecycle/ONFI cases. The completed series passes 1,272
  lifecycle/ONFI cases, including fault unwinding and scan-before-publication.
- All 20 `NandBbmInitialization` expectations match, including controls
  for cached late flags, unconditional compensation and vendor compensation.
- The exact randomizer check passes 500 source-helper observations and
  256 ECC-table/byte-stream observations. All 20 associated phase/policy
  model expectations match, including deliberate corruption controls.
- All 119 OOB-pipeline expectations match, replaying 430 source-generated
  traces and retaining deliberate mutation counterexamples.
- The pinned historical randomizer check still detects its 50 expected
  late-initialization marker failures. Thirteen Python unit tests pass.
- ARM32 LLVM and ARM64 GCC final-tree object builds pass with `W=1`.
  All 26 b4 checks pass; strict checkpatch passes for the changed first patch.

The randomizer fixture formerly inferred compensation from a non-NULL
callback. That inference fails for a shared callback with an internal
flag guard. The fixture now invokes the actual callback on a zero marker,
records the resulting mask, and checks all four bytes against the
independent LFSR oracle. It also still checks real read/write accessor
outputs and register transitions. No source behavior or expected marker
representation was changed to accommodate the fixture.

The deferred-initialization and generation-policy models retain their
historical results. They are not used as proof that the minimal draft
performs those discarded initialization steps.

## Review and limits

Review followed Sashiko's technical-pattern, callstack, false-positive,
subjective and I/O-accessor guidance. The prompt snapshot has no
MTD-specific guide. The core attach → manufacturer-init → BBT ordering,
both protected-OOB accessors, ordinary/vendor selection and cleanup
were checked against the actual kernel source.

No new driver defect was identified in this reduced delta. The separate
H616 mandatory-reset binding ABI review remains unresolved; restoring
v10's reset patches does not resolve it. A hypothetical late-scrambling
SLC device would still need subpage-geometry review; the known late-setting
Toshiba/Hynix parts are MLC. This work neither changes that boundary nor
establishes universal hardware guarantees.

These are host/source/model checks and replay of earlier captures, not
a fresh board deployment, physical Toshiba test, or new H616 measurement.
