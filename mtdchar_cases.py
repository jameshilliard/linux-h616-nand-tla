# SPDX-License-Identifier: GPL-2.0-or-later
"""Exhaustive small requests, realistic ratios, mutations and assumptions."""
from itertools import product

# Official stable release, independently pinned from the historical NAND runs.
# The rolling v1.8.0 prerelease replaces its asset in place, breaking downloads.
TLC = dict(tlc_version="1.7.4", tlc_build="2024-08-08",
           tlc_revision="5a47802b5c391f59ecdd44117981f4ff8c0656ba",
           tlc_sha256="936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88",
           tlc_url="https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar",
           tlc_cache_file="tla2tools-1.7.4.jar", tlc_json_traces=False)

MODEL = dict(invariants=["BufferBounds", "ByteIdentity", "ExactlyOnce", "Progress",
                         "OwnedDuringIO", "ReturnedClean", "Complete", "EccPriority"],
             actions=["Validate", "AllocateData", "AllocateOob", "AllocationFailure",
                      "Loop", "Prepare", "CopyData", "CopyOob", "CopyFailure",
                      "Bounds", "Page", "BackendFailure", "BackendReturn",
                      "Account", "Finish"], toolchain=TLC)


def request(w=4, o=3, pages=2, start=4, data=20, spare=15,
            read=True, symmetric=False, avail=None, size=None, has_data=None):
    return dict(width=w, spare=o, avail=o if avail is None else avail,
                erase=pages*w, start=start, data=data, oob=spare,
                size=12*w if size is None else size, read=read,
                symmetric=symmetric, hasData=data > 0 if has_data is None else has_data)


def cases():
    result = []

    def add(name, rows, mutation="none", expected=None, faults=False, kind="positive"):
        result.append(dict(module="NandMtdCharChunks", name="mtdchar-"+name,
                           expected=expected, kind=kind,
                           constants=dict(Requests=tuple(rows), Faults=faults,
                                          Mutation=mutation)))

    # Every input length/column, not just whole-page payloads, for each ratio.
    for w, o, pages in ((2, 1, 2), (2, 3, 2), (2, 5, 1), (4, 3, 2)):
        rows = [request(w, o, pages, w+col, d, ob, read, sym)
                for col, d, ob, read, sym in product(
                    range(w), range(3*w+1), range(3*o+1), (False, True), (False, True))]
        add(f"exhaustive-w{w}-o{o}-e{pages}", rows)

    fault_rows = []
    for o, avail, read, sym in product((0, 3, 5, 13), (0, 1), (False, True), (False, True)):
        for data, spare in ((0, 0), (0, 10), (13, 0), (13, 10)):
            fault_rows.append(request(o=o, avail=avail if o else 0, data=data,
                                      spare=spare, read=read, symmetric=sym))
    add("all-fault-positions", fault_rows, faults=True)
    # Requested spare larger than payload is a historical partial-success ABI.
    add("excess-oob-and-eof", [request(data=d, spare=o, start=start, read=read,
                                      symmetric=sym, size=24)
        for d, o, start, read, sym in product((0, 1, 4, 9), (0, 1, 4, 20),
                                             (0, 3, 20, 24), (False, True), (False, True))])
    add("nonnull-empty-data", [request(data=0, spare=o, has_data=True, read=read)
                                for o, read in product((0, 1, 10), (False, True))])
    # Scale 1024/1152 by 128; retain its exact ratio and 64-page buffer limit.
    add("boot0-ratio", [request(w=8, o=9, pages=64, start=8+col,
                                data=count*8-col, spare=count*9,
                                read=read, symmetric=sym, size=140*8)
        for count, col, read, sym in product((56, 57, 64, 65, 112, 128), (0, 3),
                                             (False, True), (False, True))])
    # Individual reversions must produce a named invariant counterexample.
    controls = [
        ("small-oob-buffer", request(w=2,o=5,pages=1,data=8,spare=20), "Progress"),
        ("split-oob-page", request(w=4,o=3,data=0,spare=18), "ByteIdentity"),
        ("uncoupled-data", request(w=2,o=3,data=10,spare=15), "ByteIdentity"),
        ("uncoupled-read-oob", request(w=4,o=1,start=5,data=36,spare=10,
                                      symmetric=True), "ByteIdentity"),
        ("unaligned-data-chunk", request(w=4,o=1,start=5,data=20,spare=6), "ByteIdentity"),
        ("no-oob-advance", request(data=0,spare=18), "ByteIdentity"),
        ("leak-oob", request(), "ReturnedClean"),
    ]
    for mutation, row, expected in controls:
        add("mutation-"+mutation, [row], mutation, expected, kind="mutation")
    add("assumption-zero-success", [request()], "zero-success", "Progress", kind="assumption")
    add("assumption-short-oob", [request(data=0,spare=18)], "short-success",
        "ByteIdentity", kind="assumption")
    return result
