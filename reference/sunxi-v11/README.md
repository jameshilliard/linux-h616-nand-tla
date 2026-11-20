# Source pair for the November 20 v11 review

Both candidates are based on Linux NAND-next `ec1e844b9486302a488abc03ea918db60709766e`.
The series tips below identify drafts, not released kernel versions.

| Driver | Kernel commit | SHA-256 of `drivers/mtd/nand/raw/sunxi_nand.c` |
| --- | --- | --- |
| Fix-only v11 | `7c809a984f08cc4ada5ba5f8c6abb38f0d4cd717` | `555426a171ec7993ac5cb38f16b00d5443bda9ab5b649347c402fa1d5381313f` |
| v11 plus the deferred format | `926449425acff8dd59f4b4ea08802f771c6912ea` | `b157ebf3c1d149f7fa3c395b3160f65252410bd0142056faac249b24062ea247` |

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

`small-page-ecc.c` contains the base's unmodified
`nand_ecc_sw_hamming_init_ctx()` and `nand_ooblayout_ecc_sp()`, from
`drivers/mtd/nand/ecc-sw-hamming.c` and `drivers/mtd/nand/ecc.c`
respectively. The small-page fixture also executes the on-host fallback
switch and `rawnand_sw_hamming_init()` from `reference/nand_base.c`.
Allocation and request-buffer setup are host shims; this check concerns
layout ownership and physical parity offsets, not Hamming data correction.
