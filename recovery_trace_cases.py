# SPDX-License-Identifier: GPL-2.0-or-later
"""Recovery trace schema and deliberately broken protocol-checker inputs."""
import copy

MODEL = dict(invariants=["ProtocolValid", "SnapshotsMatchC", "FreshContext", "CleanIrqReturn",
                         "HelperCleanup", "FinalAccounting", "NormalMappingSafe",
                         "NormalFreshCompletion", "ContractOutcome"], actions=["Advance"])
KINDS = {"select", "map", "prepare", "launch", "arrive", "arrive-old", "wait-start",
         "reinit", "mask", "ack", "complete", "irq-off", "irq-on", "wait-return",
         "reset-start", "reset-ok", "terminate-ok", "terminate-error", "abort-return",
         "unmap", "cleanup-return", "finish"}


def freeze(value):
    if isinstance(value, list):
        return tuple(freeze(v) for v in value)
    if isinstance(value, dict):
        return {k: freeze(v) for k, v in value.items()}
    return value


def case(name, traces, expected=None, kind="positive"):
    return dict(name="recovery-trace-" + name, module="NandRecoveryTrace", kind=kind,
                constants=dict(Traces=freeze(traces)), expected=expected)


def controls(good, timeout):
    tests = []

    def modified(name, trace, invariant, edit):
        altered = copy.deepcopy(trace)
        edit(altered["events"])
        tests.append(case(name, [altered], invariant, "mutation"))

    def first(events, kind):
        return next(e for e in events if e["kind"] == kind)

    modified("wrong-snapshot", good, "SnapshotsMatchC",
             lambda es: first(es, "map").update(mapped=False))
    modified("wrong-direction", good, "ProtocolValid",
             lambda es: first(es, "unmap").update(arg=1-good["direction"]))
    modified("wrong-context", good, "FreshContext",
             lambda es: first(es, "select")["context"].__setitem__(2, 0))
    modified("missing-unmap", good, "HelperCleanup",
             lambda es: es.pop(next(i for i, e in enumerate(es) if e["kind"] == "unmap")))
    modified("wrong-timeout-result", timeout, "ProtocolValid",
             lambda es: first(es, "wait-return").update(arg=0))

    def early_unmap(es):
        event = copy.deepcopy(first(es, "unmap"))
        es.insert(next(i for i, e in enumerate(es) if e["kind"] == "launch") + 1, event)
    modified("early-unmap", good, "MappingSafe", early_unmap)

    def restore_mask(es):
        event = copy.deepcopy(first(es, "mask"))
        es.insert(next(i for i, e in enumerate(es) if e["kind"] == "unmap"), event)
    modified("late-mask-restore", good, "CleanIrqReturn", restore_mask)
    modified("missing-abort", timeout, "ProtocolValid",
             lambda es: es.pop(next(i for i, e in enumerate(es) if e["kind"] == "abort-return")))
    return tests
