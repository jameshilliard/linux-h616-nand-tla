# BootROM format in a single partition map

This is an experimental follow-up to the sunxi NAND v11 draft. It is not
part of the previously posted series and has not been booted on hardware.
The controller's transfer, ECC and randomizer operations are unchanged.

## Representation

```dts
partitions {
    compatible = "fixed-partitions";
    #address-cells = <1>;
    #size-cells = <1>;

    partition@0 {
        compatible = "allwinner,sun50i-h616-nand-boot0";
        reg = <0x0 0x40000>;
        label = "boot0-0";
    };

    partition@100000 {
        reg = <0x100000 0x400000>;
        label = "uboot";
    };
};
```

Every `reg` describes physical NAND main-area bytes. The first example has
256 KiB of physical main area and exposes 128 KiB of decoded BootROM payload.
Its logical page is 1024 data bytes plus 1152 raw OOB bytes on the tested
2048+128-byte NAND. Normal entries retain native geometry.

The compatible selects a format, not another alias over the same range.
Linux reuses the fixed-partition parser, then routes tagged entries to
BootROM views and the rest to normal MTD partition registration. Read-only
flags are preserved. Any overlap involving an enabled BootROM entry is
rejected before publication. An all-BootROM map does not call native
registration with an empty list, which would expose the whole chip.

The existing command-line parser remains in use for ordinary devices.
A map containing BootROM-compatible entries is taken from DT: the Linux
command-line syntax cannot represent their different geometry. In the
firmware integration, U-Boot still derives its physical layout from
`mtdparts`, but preserves format properties only on exact range matches.
Missing, partial and duplicate matches fail the fixup and restore its input
DT. Older sibling `boot-areas` handoffs remain supported by U-Boot.

The board configuration disables `CONFIG_MTD_PARTITIONED_MASTER`. Enabling
that explicit diagnostic interface can still expose the whole native master
when ordinary partitions are registered; this prototype does not change
that generic MTD policy.

## Sources

The kernel prototype has three commits, atop the local corrected lifetime
and mtdchar prerequisite stack:

- `9590f9bc6990`: separate partition parsing from registration.
- `374156f42997`: describe BootROM regions in the fixed-partition binding.
- `b566d825e514`: route sunxi partitions by format.

U-Boot's integration adds `b97d5ae12f6` for format preservation and
`23ecd06819b` for the standard `read-only` property spelling. The latter
also fixes the previously documented native partition write-protection
handoff problem.

These commits are not published prerequisites. The source fragments needed
for the host checks are pinned in this repository; the checks can also
extract functions directly from a candidate kernel or U-Boot tree.

## Reproduction

```sh
python3 run_boot0_partitions.py
python3 run_uboot_partitions.py

python3 run_boot0_partitions.py --kernel /path/to/linux
python3 run_uboot_partitions.py --source /path/to/u-boot
```

The first suite runs 224 ASan/UBSan cases using the actual sunxi selection,
validation, BootROM registration and cleanup functions. Cases include all
127 nonempty BootROM/native assignments in a seven-entry map, disabled and
read-only entries, normal UBI metadata, wide offsets, invalid geometry,
overlaps, parser errors, all allocation failures and each registration
failure. Parser output and MTD publication are simulated. The suite verifies
cleanup of the driver's allocations and its parser reference, not actual
kernel reference-count concurrency.

The second suite runs 59 libfdt/ASan/UBSan layouts twice each. It includes
the earlier alias-free handoffs and new one-map cases, 32- and 64-bit DT
cells, read-only/disabled format tags, missing/partial/overlapping ranges,
error rollback and native UBI tagging. A compiled board DT was also passed
through the real fixup functions and checked for four BootROM entries and
three ordinary entries.

ARM64 builds of `mtdcore`, `mtdpart` and `sunxi_nand` pass. Both binding
documents and the controller example validate, as do the controller and
BootROM nodes in the board DT. The complete U-Boot/SPL build passes. SPL
payload remains 100637 bytes; packaged TOC0 is 106496 bytes within the
122880-byte limit. All 82 exported U-Boot patches apply to their pinned base.

These results are finite extracted-code checks, not a new TLA+ proof or a
hardware result. A fresh boot is still needed to validate the combined
kernel, DT and bootloader handoff. The older exported firmware kernel stack
does not yet contain this mapper integration; do not pair that kernel with
an update manifest expecting the mapped devices.

## Review

Reviewed against the locally cached Sashiko general, cleanup, allocation,
locking and build guidance, with its false-positive guide. Checked parser
ownership, the all-BootROM cleanup path, failure propagation, unchanged
native parser precedence, and DT-backed UBI attachment. UBI attachment uses
the MTD notifier and the partition's OF node, not a platform device created
by the OF partition parser.

The configured upstream prompts URL returned HTTP 404 during this pass;
the cached guidance was used and no current MTD-specific prompt was
available. Checkpatch has no code warnings; it asks whether MAINTAINERS
needs updating for the new binding. Maintainer routing needs a final check
before turning this prototype into a submission.
