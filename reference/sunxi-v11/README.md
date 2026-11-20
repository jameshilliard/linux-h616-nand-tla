# Source pair for the November 20 v11 review

Both candidates are based on Linux NAND-next `ec1e844b9486302a488abc03ea918db60709766e`.
The series tips below identify drafts, not released kernel versions.

| Driver | Kernel commit | SHA-256 of `drivers/mtd/nand/raw/sunxi_nand.c` |
| --- | --- | --- |
| Fix-only v11 | `fb993dda619f70997958289c6ba9dc8fd8abef99` | `e3caa05c9adf2623da8008f1fb9b03fc9c03b2b35e22dd6c81c18a17476f23ee` |
| v11 plus the deferred format | `fec5f732b011ccb7320b2216f81922f46fd34df6` | `56b605bc07fd5e427f092c8c54105aac8a40fb7f2bf39df09079389cb8fb0962` |

`sunxi_nand.c` is the fix-only driver. Applying `firmware.patch` to it
reconstructs the complete deferred driver, without carrying a second
nearly identical file. Both runners verify these hashes. The focused TLC
report records the pair under `input_metadata`; its top-level
`driver_sha256` identifies the older reference shared by the model suite.
`run_v11.py` compiles the separate source checks.
It can also accept a pair of explicit candidate files for a later review.
The driver files retain the kernel source notices.

`nand-core.c` is the base's unchanged `drivers/mtd/nand/core.c` (SHA-256
`efcbf598df9bde05a304366e75e0bd1126ae002155cfe3d03dad0f4685ce807d`).
The erased-buffer and erased-chunk functions are executed without editing
their bodies. The bad-block decision and identification helpers extracted
from the existing `reference/nand_base.c` match the NAND-next base. The
older full-driver reference is deliberately unchanged for historical tests.
