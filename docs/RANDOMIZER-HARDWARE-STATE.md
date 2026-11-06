# Stateful randomizer and older NFC evidence

This extension composes the exact 15-bit randomizer with command execution.
It separates the programmed seed from the active LFSR, including command
boundaries, enable/disable, byte order, chip selection, abort and reset.
It supplements the [source-policy checks](RANDOMIZER-BIT-CONFORMANCE.md)
and the [older controller contracts](HARDWARE-CONTRACTS.md), without
changing the kernel or accessing a board.

## Evidence by generation

The public [source manifest](../reference/randomizer/source.json) pins
twelve files from four Allwinner BSP variants, their shared register
functions, the seed tables and erased-spare predicates. Downloaded bytes
must match their hashes before the audit uses them.

| Evidence | A10 / A20 | A23 | A33 | H616 |
| --- | --- | --- | --- | --- |
| BSP variant | `nand_sun4i` / `nand_sun7i` | `nand_sun8iw3` | `nand_sun8iw5` | Previously pinned newer BSP |
| Seed write and enable | Seed at bit 16, enable bit 9 | Same | Same | Seed at bit 16, enable bit 5 |
| Disable helper | Clears enable, preserves seed | Same | Same | Linux preserves seed; newer BSP also restores `0x4a80` |
| 128 page seeds | All entries match the pinned newer table | Same | Same | All 128 compared arithmetically; 64 selected by the measured NAND geometry |
| Erased-spare signatures | Both fixed predicates match exact LFSR bytes | Same | Same | Independent physical-stream captures |
| Page/block and direction fields | Family-layout hypothesis, not established here by a register manual | Same limitation | Manual describes bits 11 and 10 | Manual describes bits 7 and 6 |
| NORMAL reload, partial-word tail, disabled interval | Unmeasured; explore alternatives | Same | Same | Constrained by the recorded experiments |
| ECC/PAGE codeword reload | BSP-compatible contract checked separately from uncertain hardware | Same | Same | Native-page captures constrain block-mode behavior |
| Reset/chip-switch effects on hidden LFSR | Unmeasured alternatives | Same | Same | Still unmeasured in isolation |

The older sources are the vendor's
[Linux 3.4 NAND BSP](https://github.com/allwinner-zh/linux-3.4-sunxi/tree/6964d467510849e3e262518cb87bff7ef92e01f5/modules/nand),
not an unmerged Linux implementation. `NFC_SetRandomSeed()`,
`NFC_RandomEnable()` and `NFC_RandomDisable()` are byte-identical across
the four pinned variants. A small fixture executes those unchanged
bodies with ASan/UBSan over every valid 15-bit seed and 256 patterned
low-register values: 8,388,608 register sequences. This establishes
software register updates, not the hardware side effects of those writes.

The [A33 NFC specification](https://linux-sunxi.org/images/4/49/A33_Nand_Flash_Controller_Specification.pdf)
revision 1.0, sections 1.4.10 and 1.4.14, describes command and randomizer
control fields. Its primary-text search index was accessible, but the
direct PDF returned HTTP 404 during this audit; no complete-PDF review
or verified PDF digest is claimed. The A10 manual provides only an NFC
overview. We do not treat the A33 document as direct documentation for
every earlier SoC. In particular, the A10/A20/A23 cases do not enable
the page-size randomizer mode. Their MSB cases are conditional checks
of the family-layout hypothesis, not observed support.

## Independent older-BSP byte evidence

The older `nand_phy.c` implementations recognize these decoded spare
bytes when checking erased pages:

| Page selector | Seed | Four decoded bytes after 1024 main bytes |
| --- | --- | --- |
| Page 0 | `0x2b75` | `0a 53 b8 c2` |
| Page modulo 128 = 127 | `0x76db` | `32 43 aa 4e` |

Applying the independent LFSR to erased `0xff` data reproduces both
predicates exactly: normalize to 15 bits, advance 15 bits, then consume
1024 bytes before extracting the four spare bytes. The audit checks
the predicates and page guards in all four BSPs, rather than merely
comparing another copy of Linux's seed table.

These constants corroborate the recurrence, bit order and phase at that
spare boundary. They do not establish arbitrary NORMAL-command restart,
page-mode continuity, parity ordering or abort recovery. The full
synthetic codeword expectations in TLC remain conditional on the
per-codeword contract; only those four bytes have this independent
older-BSP corroboration.

## Controller model

`NandRandomizerController` retains the control register, active 15-bit
state, a separate fifteen-bit-array implementation, current chip,
command column, accepted output and successful read publication.
The validity flag is model bookkeeping, not a hardware register bit.

The model covers:

- NORMAL, ECC and PAGE commands, reads and writes, and mixed sequences.
- Seed and mode changes without equating register contents to LFSR state.
- Enable/disable transitions, including disabled commands between
  randomized commands.
- Main/user/parity stream boundaries and per-codeword reseeding.
- MSB/LSB order, nonzero columns, short commands and partial-word tails.
- Abort at every prefix of a short command, including after its last
  byte but before successful completion.
- NAND reset, controller soft reset, bus reset and chip selection as
  distinct events, followed by reuse with another seed.
- Initially valid and invalid active state, with zero, one and `0x7fff`
  states, so a fresh-command check cannot rely only on cold initialization.

The existing arithmetic models exhaust the input-seed transition domain
and compare all 128 page seeds through 512- and 1024-byte phases. The
new Python tests additionally compare 520 two-codeword transfers using
all 128 seeds plus zero and `0x7fff`, both step sizes and both directions.
Those are mathematical tests on synthetic codewords, not new captures.

Two profile forms deliberately answer different questions:

1. `profile()` leaves older undocumented reload rules nondeterministic.
   The envelope checks exact arithmetic and disabled-byte bypass without
   asserting a particular old-controller stream. It is not a complete
   enumeration of every possible hardware implementation.
2. `bsp_contract_profile()` fixes ECC/PAGE launch and block boundaries to
   reseed. It checks the byte representation and reuse properties
   **conditional on that BSP-compatible contract**. It does not turn the
   contract into measured silicon behavior.

Uncertainty cases deliberately fail if asked to prove that a seed write,
mode change, chip selection or NAND reset necessarily rewinds a running
stream. Separate older-controller cases expose the unsupported assumption
that NORMAL commands or ECC block boundaries must restart. These are
limits of the available evidence, not newly discovered driver bugs.

The model's register reset value is a selected specification context.
Recovery checks explicitly program the next seed and mode; they do not
rely on hidden-state reset. Passing them says nothing about outstanding
DMA retirement or the previously measured post-abort data-placement
anomaly. Those have separate models and hardware records.

## H616 replay and sensitivity

The stateful bit-array implementation independently replays the immutable
[extended H616 capture](HARDWARE-EXTENDED-20261104.md):

- 3,072 normal-stream comparisons across recorded seeds, columns and modes.
- 48 read/write enable-transition traces.
- 64 native-page physical-image comparisons, using independent BCH
  encoding and the recorded PIO/MDMA results.

This is 3,184 comparisons with existing measurements, not a fresh hardware
run. The capture retains its original v10 source identity; the replay
does not relabel it as a v11 measurement.

TLC uses captured byte strings directly for selected transition cases.
Native-page expectations use independent BCH/LFSR bytes whose assembled
image hashes are also compared against the captures. The numeric model
and bit-array implementation must agree at every byte.

Ten deliberate mutations must be detected: treating register writes as
active-state replacement, reseeding on re-enable, missing command or
codeword restart, advancing while disabled, treating column as phase,
wrong MSB handling, randomizing the bypassed tail, freezing phase at that
tail, and publishing a failed read. Named counterexamples are required;
a parse error, timeout or unrelated invariant failure is not success.

## Running

```sh
python3 tests/test_randomizer_controller.py
python3 run_randomizer_hardware.py --download

# Offline: use bundled, hash-pinned BSP function bodies and stored captures.
python3 run_randomizer_hardware.py --source-only
```

`--download` also verifies all twelve full BSP files against the manifest;
without it, the result explicitly records that the online source audit
was not requested. `--source-only` skips TLC, not byte replay or the
sanitized BSP helper test. The runner is included in `run_suite.py` and
the CI template; no hosted CI execution is claimed.

## Remaining limits

Older NFC command-reload timing, direction-mode availability on the
earliest variants, partial-word behavior and reset side effects still
need older hardware or more precise documentation. More TLC states
cannot resolve them. These profiles must not be used to justify copying
H616-specific quirks into those drivers.

This is a serialized command/stream model, not RTL, analog timing, a BCH
decoder or a NAND wear/power-loss model. BCH has separate independent
encoding/decoding tests. The randomizer model does not establish DMA
completion, memory ordering, multi-controller concurrency, a clock-stalled
engine's progress or complete correctness of the Linux driver.

## Validation record, 2026-11-05

| Check | Result |
| --- | --- |
| Public BSP audit | All twelve file hashes, shared functions, seed tables and fixed spare predicates matched |
| Unchanged BSP helper fixture | 8,388,608 register sequences passed with ASan/UBSan |
| Stateful replay | All 3,184 stored H616 comparisons matched |
| Controller TLC | 48 positive configurations, 10 mutation controls and 8 uncertainty witnesses matched |
| Model action coverage | Config, Reset, Start, Byte, Finish, Abort and End all reached |
| Python controller tests | Eight passed, including 520 synthetic two-codeword transfers |
| Result-classification tests | Three passed |
| Broader source/capture regression suite | All 19 jobs passed, with unchanged executable inputs |

The candidate source used for the broader regression suite was the v11
driver with SHA-256
`159e0501b8906e74cd0a192724d61ddcab08a644842686e664db098f39151153`.
The controller model itself consumes the pinned BSP evidence and stored
captures, not that candidate C file. No new confirmed kernel defect or
physical hardware behavior was discovered in this extension.
