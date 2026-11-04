# SPDX-License-Identifier: GPL-2.0-or-later
"""Cross-command stream, address-aperture and persistent-register contracts."""


def extend_contracts(modules, add, mutation):
    modules.update({
        "NandMeasuredStream": dict(
            invariants=["MeasuredContract", "RoundTrip", "Bounds"],
            actions=["Write", "Turnaround", "Read", "Finish"]),
        "NandRandomizerStream": dict(
            invariants=["PhysicalEncoding", "IndependentDecode", "CleanReturn",
                        "SuccessfulPrefix", "NoCommandsForZero", "NoReplayAfterError"],
            actions=["Begin", "Configure", "Issue", "Transfer", "Copy", "Cleanup", "Finish"]),
        "NandDmaAperture": dict(
            invariants=["BufferRepresentable", "DescriptorRepresentable", "ExactAddress",
                        "NoZeroDescriptor", "BalancedMaps"],
            actions=["Mask", "Allocate", "Map", "Validate", "Encode", "Launch", "Unmap"]),
        "NandUserDataContext": dict(
            invariants=["ActiveLengths", "UnusedCleared", "BankBounds", "LegacyNoLengthMMIO"],
            actions=["Begin", "Clear", "Pio", "Program", "Consume", "Finish"]),
    })
    for size in (4, 8, 12, 16):
        for chunk in (4, 8, 16):
            add("NandMeasuredStream", f"measured-block-stream-{size}-{chunk}",
                dict(Size=size, WriteChunk=chunk, ReadChunk=chunk, PageMode=False))
        add("NandMeasuredStream", f"measured-single-write-page-read-{size}",
            dict(Size=size, WriteChunk=16, ReadChunk=4, PageMode=True))
    for size, write, read, mode, name in (
        (8, 4, 8, False, "changed-command-boundaries"),
        (8, 4, 4, True, "page-mode-write-read-asymmetry"),
        (6, 3, 3, False, "unrandomized-last-read-byte"),
        (9, 4, 4, False, "unaligned-final-command"),
    ):
        add("NandMeasuredStream", "measured-witness-" + name,
            dict(Size=size, WriteChunk=write, ReadChunk=read, PageMode=mode),
            kind="hardware-contract", expected="RoundTrip")
    base = dict(Size=9, WriteChunk=4, ReadChunk=3, PageMode=False)
    for change in ("writer-continuous", "reader-continuous", "omit-read-tail"):
        mutation("NandMeasuredStream", base, change, "MeasuredContract")
    def op(read=False, random=True, seed=1, size=9, chunk=4, fail=0):
        return dict(read=read, random=random, seed=seed, size=size, chunk=chunk, fail=fail)
    for read in (False, True):
        for chunk in (2, 4):
            for fail in (0, 1, 2, 3):
                for random in (False, True):
                    operations = (op(read, random, chunk=chunk, fail=fail),
                                  op(not read, random, seed=2, chunk=3),
                                  op(read, False, size=0), op(not read, False, size=1))
                    add("NandRandomizerStream", f"stream-{read}-{random}-chunk{chunk}-fail{fail}",
                        dict(Operations=operations, ReloadAtCommand=False))
    stream = dict(Operations=(op(), op(seed=2)), ReloadAtCommand=False)
    mutation("NandRandomizerStream", stream, "reseed-chunk", "PhysicalEncoding")
    mutation("NandRandomizerStream", stream, "stale-seed", "PhysicalEncoding")
    mutation("NandRandomizerStream", stream, "continue-next-operation", "PhysicalEncoding")
    mutation("NandRandomizerStream", dict(stream, Operations=(op(fail=2), op(random=False))),
             "dirty-error", "CleanReturn")
    for read, expected in ((False, "PhysicalEncoding"), (True, "IndependentDecode")):
        add("NandRandomizerStream", f"stream-assumption-command-reload-{read}",
            dict(stream, Operations=(op(read=read),), ReloadAtCommand=True),
            kind="assumption", expected=expected)

    aperture = dict(Backend="descriptor", Address=(0x8000, 0xffff), Length=1024,
                    Descriptor=(0xfff0, 0xffff), InheritedBits=32,
                    MapHonorsMask=True, AllocHonorsMask=True)
    for backend in ("external", "register", "descriptor"):
        for address in ((0, 0), (0xfc00, 0xffff), (0xfff8, 0xffff), (0, 0x10000)):
            for length in (8, 1024):
                add("NandDmaAperture", f"aperture-{backend}-{address}-{length}",
                    dict(aperture, Backend=backend, Address=address, Length=length))
    for desc in ((0xfff0, 0xffff), (0xfff4, 0xffff), (0, 0x10000), (1, 0)):
        add("NandDmaAperture", f"aperture-descriptor-allocation-{desc}", dict(aperture, Descriptor=desc))
    for length in (0, 7, 65528, 65535, 65536):
        add("NandDmaAperture", f"aperture-descriptor-length-{length}",
            dict(aperture, Address=(0, 0), Length=length))
    mutation("NandDmaAperture", dict(aperture, Address=(0, 0x10000), InheritedBits=64),
             "omit-mask", "BufferRepresentable")
    mutation("NandDmaAperture", aperture, "cpu-address", "ExactAddress")
    for name, changed, expected in (
        ("high-map", dict(Address=(0,0x10000), MapHonorsMask=False), "BufferRepresentable"),
        ("end-overflow", dict(Address=(0xfff8,0xffff), MapHonorsMask=False), "BufferRepresentable"),
        ("high-descriptor", dict(Descriptor=(0,0x10000), AllocHonorsMask=False), "DescriptorRepresentable"),
        ("unaligned-descriptor", dict(Descriptor=(1,0), AllocHonorsMask=False), "DescriptorRepresentable"),
        ("zero-request", dict(Length=0, MapHonorsMask=False), "NoZeroDescriptor"),
    ):
        add("NandDmaAperture", "aperture-assumption-" + name, dict(aperture, **changed),
            kind="assumption", expected=expected)

    def batch(user, first=0, count=None, dma=True):
        return dict(user=tuple(user), first=first, count=len(user)-first if count is None else count, dma=dma)
    wide = batch([4] * 32)
    mixed = batch([0,4,8,0,4,8,0,4,8])
    for first in (0, 1, 7, 8, 15, 16, 31):
        user = tuple((i % 9) * 4 for i in range(32))
        operations = (wide, batch(user, first, 1, False), mixed,
                      batch(user, first, 1), batch([0], dma=False), wide)
        add("NandUserDataContext", f"userdata-context-logical{first}",
            dict(Operations=operations, VariableLengths=True))
    for count in (1, 7, 8, 9, 16, 17, 31, 32):
        add("NandUserDataContext", f"userdata-context-count{count}",
            dict(Operations=(wide, batch([0]*count), mixed, wide), VariableLengths=True))
    legacy = dict(Operations=(batch([4]*16), batch([4], dma=False)), VariableLengths=False)
    add("NandUserDataContext", "userdata-context-legacy-fixed", legacy)
    base = dict(Operations=(wide, batch([4,8,0], first=1, count=2)), VariableLengths=True)
    mutation("NandUserDataContext", base, "wrong-logical-base", "ActiveLengths")
    mutation("NandUserDataContext", base, "skip-zero", "ActiveLengths")
    mutation("NandUserDataContext", base, "skip-unused", "UnusedCleared")
    mutation("NandUserDataContext", legacy, "legacy-write", "LegacyNoLengthMMIO")
