# Partition-local BootROM format

The current v11 draft uses an `allwinner,boot-format` boolean on ordinary
fixed-partition nodes. It selects the NAND encoding consumed by the SoC's
BootROM, not the contents or a firmware image name. The new kernel mapper
uses `sunxi_nand_boot_*` identifiers and `SUNXI_NAND_BOOT_*` constants.

```dts
partitions {
    compatible = "fixed-partitions";
    #address-cells = <1>;
    #size-cells = <1>;

    partition@0 {
        reg = <0x0 0x40000>;
        label = "spl";
        allwinner,boot-format;
    };

    partition@100000 {
        reg = <0x100000 0x400000>;
        label = "uboot";
    };
};
```

The `reg` ranges describe physical main-area bytes. Boot-format entries
expose 1024 payload bytes per 2048-byte physical page, with the remainder
available as raw OOB. For 2048+128-byte NAND this is 1024+1152 logical
geometry. The hardware encoding and transfer operations are unchanged.

The driver selects the fixed map when any entry has this property, including
disabled entries. It filters disabled entries, preserves `read-only` and
rejects overlaps involving enabled boot-format regions. Unmarked entries
use native geometry. A wholly boot-format map does not register a native
whole-chip fallback. The explicit `CONFIG_MTD_PARTITIONED_MASTER` interface
can still expose the native master for a mixed map; callers must not use it
or runtime-created aliases to access reserved boot-format ranges.

## Reproduction

```sh
python3 run_boot_format_partitions.py
python3 run_boot_format_partitions.py --kernel /path/to/linux
python3 run_boot_format_lifetime.py --kernel /path/to/linux
```

The partition checker pins the actual seven routing, validation, registration
and cleanup functions from the v11 draft at `3ae44d13a5ec`. Its 226
ASan/UBSan cases include all 127 nonempty boot/native assignments in a
seven-entry map, disabled entries, read-only flags, all-boot maps, wide
offsets, overlap/geometry rejection, allocation failures and publication
failures. Two selector tests distinguish the boolean from a partition's
name or unrelated compatible. Parser output, MTD publication, locks and
reference ownership are explicit shims.

The lifetime checker runs 48 bounded combinations of chips, views,
registration, retained references and suspended waiters. It confirms that
NAND disconnection precedes draining the view mutexes. Historical negative
controls use the corresponding older checker and source revision from Git;
the current extractor expects the renamed identifiers.

The local kernel checks also pass: ARM64 `W=1` builds of `sunxi_nand` and
`mtdpart`, binding/example validation, 15 positive/negative schema cases,
and extracted transfer/erase and native-OOB ownership sanitizer fixtures.
All 108 prerequisite/outgoing patches apply without three-way merging.
The historical v2-v10 changelogs match the sent version verbatim.

Reviewed against the available Sashiko general, DT-binding, locking,
allocation, cleanup and build guidance, with its false-positive guide.
No MTD-specific prompt is present in that prompt checkout. Checkpatch's
remaining notice asks whether the new documentation needs a MAINTAINERS
entry; there are no code errors or alignment findings.

These are bounded source checks, not a new TLA+ proof or hardware test.
The single-map handoff still needs an integration/board run with the boolean
property. Earlier hardware records and the compatible-based prototype keep
their original names and apply only to their recorded revisions. The
unposted retained-lifetime v4 corrections remain a submission dependency
gate; this validation does not imply readiness to send.
