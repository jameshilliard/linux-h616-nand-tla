# SPDX-License-Identifier: GPL-2.0-or-later
"""Timing-fold quotient cases and source-refinement module contracts."""

PROFILES = (
    dict(name="a10-a23", setup=1,
         tables=((6, 12, 16, 20), (7, 15, 23, 31),
                 (7, 15, 23, 31), (4, 8, 12, 20))),
    dict(name="h6-h616", setup=2,
         tables=((28, 44, 60, 76), (0, 12, 28, 44),
                 (0, 12, 28, 44), (8, 24, 40, 56))),
)


def divisors(profile):
    s = profile["setup"]
    return (s, s, s, s, 1, 1, s, 1, 1, 3, s, 1, 1, 2, 2,
            *(t[-1] for t in profile["tables"]))


FOLD = dict(invariants=["ExactMinimum", "Nonnegative"],
            actions=["Merge", "ReadAccess"])
PROGRAM = dict(invariants=["LookupFits", "LookupMinimal", "EncodingFits",
                           "ValidDivisors", "CheckOnlyPure", "ClockSafe", "SourceAgreement"],
               actions=["Enter", "Merge", "ReadAccess", "Lookup", "Check",
                        "Cache", "Clock", "Edo"])


def extend_timing(modules, add, mutation):
    modules["NandTimingFold"] = FOLD
    for profile in PROFILES:
        small = dict(Divisors=divisors(profile), Durations=set(range(9)),
                     AccessTimes=set(range(10)))
        add("NandTimingFold", "timing-fold-" + profile["name"], small)
        # Values on either side of ns conversion and lookup/division boundaries.
        large = dict(small, Durations={0, 1, 999, 1000, 1001, 10001,
                                      300001, 400000, 4000000},
                     AccessTimes={0, 1001, 4000001})
        add("NandTimingFold", "timing-fold-boundaries-" + profile["name"], large)
    for name in ("floor", "overwrite", "skip-rea"):
        mutation("NandTimingFold", small, name, "ExactMinimum")
