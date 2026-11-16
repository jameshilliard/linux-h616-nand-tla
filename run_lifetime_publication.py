#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded publication/retirement contracts with independently failing mutations."""
import argparse
from pathlib import Path
import unittest

import run as tlc
from mtdchar_cases import TLC
from parser_reference import evidence, profiles


def parser_cases(source_profiles):
    base = dict(Depth=2, Target=2, InitiallyBusy=True, ConcurrentDelete=False,
                ParserPin=source_profiles["v6"]["reference_kind"],
                EarlyPut=False, PutKind="matching",
                CheckOpen=True, RejectRemovedParent=True)
    result = []
    for depth in (1, 2):
        for target in range(1, depth + 1):
            for busy in (False, True):
                for deletion in (False, True):
                    result.append(dict(
                        name=f"parser-metadata-{depth}-{target}-{int(busy)}-{int(deletion)}",
                        module="MtdParserReferences", kind="positive", expected=None,
                        constants=dict(base, Depth=depth, Target=target,
                                       InitiallyBusy=busy, ConcurrentDelete=deletion)))
    # Operational pins do retain storage when admission succeeds. The v5
    # regression is an admission failure, not a missing reference on success.
    for deletion in (False, True):
        result.append(dict(name=f"parser-operational-idle-{int(deletion)}",
                           module="MtdParserReferences", kind="positive", expected=None,
                           constants=dict(base, ParserPin="operational", InitiallyBusy=False,
                                          ConcurrentDelete=deletion)))
    for depth in (1, 2):
        result.append(dict(name=f"parser-v5-busy-{depth}", module="MtdParserReferences",
                           kind="regression", expected="RegistrationIndependent",
                           constants=dict(base, Depth=depth, Target=depth,
                                          ParserPin=source_profiles["v5"]["reference_kind"])))
    if "current" in source_profiles:
        kind = source_profiles["current"]["reference_kind"]
        result.append(dict(name="parser-current-source", module="MtdParserReferences",
                           kind="positive" if kind == "metadata" else "regression",
                           expected=None if kind == "metadata" else "RegistrationIndependent",
                           constants=dict(base, ParserPin=kind)))
    for name, delta, invariant in (
        ("missing-pin", dict(ParserPin="none", ConcurrentDelete=True), "ParserStorage"),
        ("early-put", dict(EarlyPut=True, ConcurrentDelete=True), "ParserStorage"),
        ("wrong-put", dict(PutKind="operational"), "ProviderBalance"),
        ("missing-put", dict(PutKind="none"), "PinsDrained"),
        ("bypass-open", dict(CheckOpen=False), "OpenAdmission"),
        ("removed-parent", dict(RejectRemovedParent=False, ConcurrentDelete=True),
         "NoOrphanChild"),
    ):
        result.append(dict(name="parser-" + name, module="MtdParserReferences",
                           kind="mutation", expected=invariant, constants=dict(base, **delta)))
    return result


def cases(source_profiles=None):
    if source_profiles is None:
        source_profiles = profiles()
    partition = dict(EarlyPublish=False, HoldParseRef=True, RejectRemovedParent=True,
                     KeepSiblings=True)
    cell = dict(Refresh=True, ExplicitLayout=True, RemoveBeforeFree=True,
                PairedNotifier=True, ExplicitOwner=True)
    result = []
    for module, base, mutations in (
        ("MtdPartitionPublication", partition, (
            ("early-list", dict(EarlyPublish=True), "ClassDevicePublished"),
            ("no-parse-ref", dict(HoldParseRef=False), "ParserStorage"),
            ("removed-parent", dict(RejectRemovedParent=False), "NoOrphanChild"),
            ("sibling-rollback", dict(KeepSiblings=False), "SiblingsSurvive"))),
        ("NvmemCellPublication", cell, (
            ("stale-group", dict(Refresh=False), "AllCellAttributes"),
            ("implicit-layout", dict(ExplicitLayout=False), "ProviderCellsSurvive"),
            ("early-free", dict(RemoveBeforeFree=False), "ActiveAttributesAlive"),
            ("unpaired-remove", dict(PairedNotifier=False), "NotificationsPaired"),
            ("owner-fallback", dict(ExplicitOwner=False), "ControllerNotPinned")))):
        result.append(dict(name=module + "-fixed", module=module, kind="positive",
                           expected=None, constants=base))
        for name, delta, invariant in mutations:
            result.append(dict(name=module + "-" + name, module=module, kind="mutation",
                               expected=invariant, constants=dict(base, **delta)))
    return result + parser_cases(source_profiles)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--kernel-tree", type=Path)
    parser.add_argument("--revision")
    args, remaining = parser.parse_known_args()
    if args.revision and not args.kernel_tree:
        parser.error("--revision requires --kernel-tree")
    if "--help" not in remaining and "-h" not in remaining:
        suite = unittest.defaultTestLoader.discover(
            str(Path(__file__).resolve().parent / "tests"), pattern="test_parser_reference.py")
        if not unittest.TextTestRunner().run(suite).wasSuccessful():
            raise SystemExit(1)
    sources = profiles(args.kernel_tree, args.revision)
    raise SystemExit(tlc.main(remaining, cases=cases(sources), toolchain=TLC,
        metadata=dict(parser_sources=evidence(sources),
                      limits="API selection/order checks and bounded contract models; "
                             "not C execution/refinement, weak-memory or hardware verification."), modules={
            "MtdParserReferences": dict(
                invariants=["TypeOK", "ParserStorage", "NoOrphanChild", "RegistrationIndependent",
                            "OpenAdmission", "OpenAfterRecovery", "ProviderBalance",
                            "MetadataBalance", "PinsDrained"],
                actions=["Pin", "Parse", "ChildReturn", "Return", "Open", "Close",
                         "Recover", "Delete", "Free"]),
            "MtdPartitionPublication": dict(
                invariants=["ClassDevicePublished", "ParserStorage", "NoOrphanChild",
                            "SiblingsSurvive"],
                actions=["Publish", "Register", "Delete", "Parse", "Put"]),
            "NvmemCellPublication": dict(invariants=["AllCellAttributes", "ProviderCellsSurvive",
                "ActiveAttributesAlive", "NotificationsPaired", "ControllerNotPinned"],
                actions=["Bind", "AddLate", "Unbind", "Rebind", "Publish", "Reader",
                         "Remove", "Drain", "Free"])}))
