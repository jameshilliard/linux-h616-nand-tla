# SPDX-License-Identifier: GPL-2.0-or-later
"""Public MTD boundary matrices and independently failing negative controls."""
from itertools import product


def request(**changes):
    q = dict(off=0, len=8, ooblen=0, ooboff=0, data=True, oob=False,
             mode="place", write=False, writable=True, dataret=8, oobret=0,
             ecc=True, flips=0, corrected=0, failed=0, stats=True, threshold=4, error="none")
    q.update(changes)
    return q


def extend_mtd(modules, add, mutation):
    modules["NandMtdBoundary"] = dict(
        invariants=["PartitionBounds", "Translation", "RejectedLengths", "ReadOnly",
                    "ReturnedLengths", "EccStats", "RequestStats", "Result"],
        actions=["NormalizeRequest", "Validate", "Translate", "Dispatch", "Account", "Finish"])
    base = dict(Size=32, PageSize=8, SpareSize=4, FreeSize=2,
                Offsets=(8, 8), WordMod=256, Policy="source")
    # All offsets here are ordinary valid/invalid bounds: wrap witnesses below
    # are isolated so the baseline matrices cannot silently bless them.
    admission = []
    for mode, off, length, olength, ooffset in product(
            ("place", "auto", "raw"), (-1, 0, 1, 24, 31, 33),
            (0, 1, 8, 32, 33), (0, 1, 4, 16, 17), (0, 1, 4)):
        admission.append(request(mode=mode, off=off, len=length, ooblen=olength,
                                 oob=True, ooboff=ooffset, dataret=0))
    # Batch configurations to keep TLC init enumeration/memory predictable.
    for i in range(0, len(admission), 180):
        add("NandMtdBoundary", f"mtd-admission-{i // 180:02d}",
            dict(base, Requests=tuple(admission[i:i + 180])))
    outcomes = []
    for write, writable, error, flips, ecc in product(
            (False, True), (False, True), ("none", "io", "ecc"), (0, 3, 4, 5), (False, True)):
        outcomes.append(request(write=write, writable=writable, error=error,
                                flips=flips, corrected=flips * 2, ecc=ecc,
                                failed=1 if error == "ecc" else 0,
                                len=16, oob=True, ooblen=4,
                                dataret=8 if error == "io" else 16,
                                oobret=2 if error == "io" else 4))
    for depth in range(4):
        add("NandMtdBoundary", f"mtd-results-depth{depth}",
            dict(base, Offsets=(8,) * depth, Requests=tuple(outcomes)))
    normalizations = tuple(request(data=data, oob=oob, len=999 if not data else 8,
        ooblen=999 if not oob else 2, dataret=8 if data else 0, oobret=2 if oob else 0)
        for data, oob in product((False, True), repeat=2))
    add("NandMtdBoundary", "mtd-normalize-absent-buffers", dict(base, Requests=normalizations))
    add("NandMtdBoundary", "mtd-optional-request-stats", dict(base, Requests=tuple(
        request(write=write, stats=stats, error=error, corrected=2, failed=1, flips=2)
        for write, stats, error in product((False, True), (False, True), ("none", "io", "ecc")))))
    eof = request(off=32, data=False, len=0, dataret=0, oob=True, ooboff=1, ooblen=1, oobret=1)
    # TLC arithmetic is bounded here; the C runner separately checks SIZE_MAX
    # on a 64-bit host. 256 is a reduced unsigned word, not an H616 size_t.
    overflow = request(off=1, len=255, dataret=0)
    for name, q in (("eof-spare-underflow", eof), ("data-addition-overflow", overflow)):
        add("NandMtdBoundary", "mtd-source-" + name, dict(base, Requests=(q,)),
            expected="PartitionBounds", kind="known-bug")
        add("NandMtdBoundary", "mtd-bounded-contract-" + name,
            dict(base, Policy="bounded", Requests=(q,)))
    add("NandMtdBoundary", "mtd-eof-fixed-spare-boundary",
        dict(base, Policy="eof-fixed", Requests=tuple(
            request(off=off, data=False, len=0, dataret=0, oob=True,
                    ooboff=ooff, ooblen=olen, oobret=0, mode=mode, write=write)
            for off, ooff, olen, mode, write in product(
                (24, 31, 32, 33), (0, 1, 2, 3, 4), (0, 1, 2, 4, 5),
                ("place", "auto", "raw"), (False, True)))))
    # This deliberately narrow fix does not change the separate length sum.
    add("NandMtdBoundary", "mtd-eof-fixed-data-addition-overflow",
        dict(base, Policy="eof-fixed", Requests=(overflow,)),
        expected="PartitionBounds", kind="known-bug")
    good = dict(base, Requests=(request(flips=4, corrected=8),))
    for name, invariant, q in (
            ("skip-parent", "Translation", request()),
            ("stale-lengths", "RejectedLengths", request(off=-1)),
            ("lost-ancestor-stats", "EccStats", request(flips=4, corrected=8)),
            ("lost-failed-stats", "EccStats", request(error="ecc", failed=2)),
            ("reset-write-stats", "RequestStats", request(write=True)),
            ("hide-error", "Result", request(error="io", dataret=0)),
            ("threshold-off-by-one", "Result", request(flips=4))):
        mutation("NandMtdBoundary", dict(good, Requests=(q,)), name, invariant)
