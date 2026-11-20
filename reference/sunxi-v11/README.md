# Source pair for the November 20 v11 review

Both candidates are based on Linux NAND-next `ec1e844b9486302a488abc03ea918db60709766e`.
The series tips below identify drafts, not released kernel versions.

| Driver | Kernel commit | SHA-256 of `drivers/mtd/nand/raw/sunxi_nand.c` |
| --- | --- | --- |
| Fix-only v11 | `84db8061e076345b5469160da2a10900d7d08e9e` | `1098df8c3c7eaca65fc893ee0b4109cba886d9131eb565fa5f77cd7ccbeb39db` |
| v11 plus the deferred format | `287a1a96d4255ff85376c46bb6088b9808fb6a16` | `37871b284a23cde56dfc669848ce49a7093383f85c634fc4768e71872c5f15f7` |

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
