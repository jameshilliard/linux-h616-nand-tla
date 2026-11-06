# Complete timing calculation: bounded proofs and C conformance

## Source and result

This extension checks the minimal v11 driver at
`ed091dc67b8d2bdfa2e1c236c279b857fce2b07b`, SHA-256
`6a4e8c05d61e02a09275a4b434203df33e0ea2be48b5c33d6454329e35f5e02b`.
The recent reset-binding commit-message amendment did not change driver bytes.
No kernel implementation change is made by this validation extension.

The earlier `NandTiming` model checked individual lookup/clock contracts.
`NandTimingFold` now checks the complete 19-constraint minimum-period fold;
`NandTimingProgram` follows it through all four lookups, cached register
encoding, clock rounding, check-only handling and EDO selection. Its final
states are compared with observations from the unchanged C helper.

The recorded run matches **17/17 TLC expectations**: six positive
configurations, nine deliberate algorithm regressions and two violated-clock
assumptions. The positive configurations visit 32,171 distinct states in
total. All named nonterminal actions are reached. The new C fixture checks
1,186 observations under ASan/UBSan and repeats them in a GCOV build;
both builds must produce identical observations. Twenty-three Python unit
tests pass, including ten new timing-adapter/oracle tests. The complete
20-job source/capture-replay suite also passes against this driver digest.

These results are bounded model checks and source conformance tests, not a
TLAPS theorem or an exhaustive proof of all C executions and silicon behavior.

## Minimum-period invariant

For constraint `i`, let `d[i]` be its required duration and `c[i]` the number
of controller cycles available. The source fold keeps period `p` unchanged
when `d[i] <= p*c[i]`, otherwise setting it to `ceil(d[i]/c[i])`.
The model checks the invariant

```text
p = max(ceil(d[j] / c[j]) for every processed j)
```

The induction argument is small: initially the maximum over no requirements
is zero. A satisfied constraint leaves that maximum unchanged; an unsatisfied
one replaces it with its strictly larger ceiling. Therefore the fold both
satisfies every processed constraint and chooses the least integral
picosecond period satisfying them. This argument requires positive divisors
and nonoverflowing arithmetic. It is explanatory reasoning, not a separate
machine-checked unbounded induction proof.

The model executes the source's comparison/update algorithm and compares it
with a second accumulator using quotient/remainder arithmetic. It retains
only the maximum processed requirement, not the complete vector: histories
with identical position and maxima have identical possible future behavior
for this invariant. That permits exhaustive exploration of every choice at
all 19 positions without enumerating every vector separately.

Both A10/A23 and H6-generation timing tables are checked with:

- Every duration independently selected from `0..8`; subsequent read-access
  duration from `0..9`, with both zero and nonzero read-output hold time.
- Every duration independently selected from
  `{0, 1, 999, 1000, 1001, 10001, 300001, 400000, 4000000}`;
  read-access duration from `{0, 1001, 4000001}`, with both hold-time cases.

The second domain exercises picosecond/nanosecond boundaries and much larger
durations without pretending to enumerate the complete `u32` domain. The
largest fold multiplication is below 305 million; addition and lookup
ceiling operations in these configurations also fit unsigned 32-bit C
arithmetic and TLC's signed 32-bit integers. Word-overflow behavior outside
these bounds is not modeled here.

The final read-access step raises the period to `tREA_max` only when
`tRLOH_min` is zero. Mutations check detection of rounding down, overwriting a
larger earlier requirement, and omitting this read-access requirement.

## Register and clock calculation

The C runner extracts `_sunxi_nand_lookup_timing()` and
`sunxi_nfc_setup_interface()` unchanged, along with both timing tables,
the timing-structure definition and actual register-packing macros.
It verifies that the model's tables/setup-cycle counts match the source and
that all 21 accessed SDR fields have the pinned kernel's `u32` types.
Drift fails validation rather than silently substituting model values.

Input vectors cover each of the 19 requirements becoming dominant,
one-picosecond boundaries, each lookup-table entry, zero-cycle encodings,
the 30 ns EDO boundary, the independent `tREA` EDO condition, slower clock
responses, zero/negative clock errors, check-only and invalid interfaces.
An additional 64 deterministic multi-constraint vectors per generation
exercise combinations. These C vectors are finite tests, not exhaustive
input-domain coverage.

For every positive source observation the stage model checks:

- Each selected lookup encoding satisfies its duration, and no earlier
  encoding would satisfy it; encodings fit their two-bit fields.
- Lookup and frequency-conversion divisors are positive.
- Check-only calls leave cached timing/clock fields untouched and never call
  the clock provider, including when that provider would return an error.
- Nanosecond rounding requests a period no shorter than the calculated one.
- EDO selection uses the returned clock rate, not the requested rate.
- Return value, packed register value, requested clock, EDO cache and provider
  call count agree with the C observation.

Successful preparation does not itself access hardware. On clock-rounding
failure the source has already updated its cached timing configuration and
requested rate, while retaining its previous EDO cache. The model preserves
that partial update; it does not invent rollback. NAND core propagates the
setup error before accepting the new interface. Later hardware selection and
clock-setting failures remain covered separately by the selection/lifecycle
checks.

The four lookup-rejection returns are not reached in this new C matrix.
That follows from the full fold: each LUT's maximum value contributed a
constraint, so with positive period and nonoverflowing arithmetic that
maximum must satisfy the duration. The standalone lookup rejection tests
remain in `run_lowlevel.py`. No rejection is fabricated just to increase
coverage.

## Clock-provider boundary

The positive clock contract is that the returned frequency is no greater
than the requested one. Since the request is
`floor(1 GHz / ceil(period_ps / 1000))`, this ensures its integral-nanosecond
period is at least the rounded-up requested period.

The negative provider fixtures return 100 MHz for a requirement that requests
10 MHz. Both generations reach the named `ClockSafe` counterexample. This
shows that the solver depends on clock behavior; it does **not** identify a
reachable H616 clock-provider bug.

The current H616 `nand0`/`nand1` clocks use `SUNXI_CCU_MP_WITH_MUX_GATE` with
no parent-rate-change flag. Source inspection follows
`ccu_mp_determine_rate()` through `ccu_mux_helper_determine_rate()` and
`ccu_mp_determine_rate_helper()` to `ccu_mp_find_best()`. That helper excludes
candidate rates above the request. `ccu_is_better_rate()` also excludes them
without `CCU_FEATURE_CLOSEST_RATE`. This supports the contract for that
configuration, but these provider bodies and the physical clock tree are not
executed by the new timing fixture.

Rate stability between setup and subsequent `clk_set_rate()`, physical pin
timings, clock tolerance and undocumented controller timing behavior remain
external contracts. A10/A23 and H616 use different source timing tables;
passing their software calculations does not newly validate either table
against silicon.

## Regression controls and reproducibility

Nine algorithm controls require specific invariant failures:

1. Floor instead of ceiling in the constraint fold.
2. Overwriting a stronger earlier constraint.
3. Omitting the zero-hold-time read-access constraint.
4. Flooring the lookup cycle count.
5. Modifying cached state during check-only.
6. Overlapping the `tCAD` and `tRHW` bit fields.
7. Flooring a sub-nanosecond period to zero.
8. Flooring a nonzero nanosecond period, changing the frequency request.
9. Selecting EDO from the requested rather than returned frequency.

The zero-period control must fail `ValidDivisors` **before** evaluating the
division. A TLC arithmetic exception is a test failure, not an accepted
counterexample. Unit tests also require rejection of corrupted C observations,
missing clock requests, timing-table drift and changed field widths.

```sh
python3 run_suite.py --module NandTimingFold
python3 run_timing.py --candidate-source /path/to/sunxi_nand.c --download
python3 -m unittest discover -s tests -p 'test_*.py'
python3 run_suite.py --candidate-source /path/to/sunxi_nand.c --source-only
```

`run_timing.py --source-only` omits TLC but retains the extracted C checks and
independent integer oracle. `run_suite.py` includes this runner automatically;
its full mode runs both the fold and C-connected models. Reports contain
source/function/fixture/model hashes, observations, coverage and named
counterexamples under ignored `results/` directories.

Review used Sashiko's technical-pattern, callstack, false-positive and
I/O-accessor guidance; that snapshot has no MTD-specific prompt. No new
driver defect was confirmed. Unsupported-clock responses and synthetic timing
vectors remain explicitly classified rather than promoted to kernel fixes.
