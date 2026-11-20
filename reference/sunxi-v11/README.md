# Source pair for the November 20 v11 review

Both candidates are based on Linux NAND-next `ec1e844b9486302a488abc03ea918db60709766e`.
The series tips below identify drafts, not released kernel versions.

| Driver | Kernel commit | SHA-256 of `drivers/mtd/nand/raw/sunxi_nand.c` |
| --- | --- | --- |
| Fix-only v11 | `22d7cd4b972ed593748a6290b1b6eaa52d042d8a` | `cc99bdbc76e0fc9ed069cdd8cc06b09e2d5f3f3e251c1c63f3982872ea2aa355` |
| v11 plus the deferred format | `7fd96b411f5612dde1f39f8a3e03d7d3d4aba4e2` | `3bcabc69afb688297c5595433a9e3e23eae9fe96d6e420672294cd051df32336` |

`sunxi_nand.c` is the fix-only driver. Applying `firmware.patch` to it
reconstructs the complete deferred driver, without carrying a second
nearly identical file. `run_v11.py` checks both hashes before compiling.
It can also accept a pair of explicit candidate files for a later review.
The driver files retain the kernel source notices.

`nand-core.c` is the base's unchanged `drivers/mtd/nand/core.c` (SHA-256
`efcbf598df9bde05a304366e75e0bd1126ae002155cfe3d03dad0f4685ce807d`).
The erased-buffer and erased-chunk functions are executed without editing
their bodies. The bad-block decision and identification helpers extracted
from the existing `reference/nand_base.c` match the NAND-next base. The
older full-driver reference is deliberately unchanged for historical tests.
