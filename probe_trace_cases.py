# SPDX-License-Identifier: GPL-2.0-or-later
"""Protocol-checker unit inputs; actual C traces are supplied by the dedicated runner."""
import copy

MODEL = dict(invariants=["ProtocolValid", "OnlyAvailableTouched", "AllScannedBeforePublish",
                         "CleanupSafe", "SnapshotsMatchC", "FinalOwnership", "SuccessComplete"],
             actions=["Advance"])
KINDS = {"scan-start", "scan-ok", "scan-error", "link", "register-ok", "register-error",
         "unregister", "unlink", "probe-ok", "probe-error", "finish"}


def freeze(value):
    if isinstance(value, list):
        return tuple(freeze(v) for v in value)
    if isinstance(value, dict):
        return {k: freeze(v) for k, v in value.items()}
    return value


def case(name, traces, expected=None, kind="positive"):
    return dict(name="probe-refinement-" + name, module="NandProbeTrace", kind=kind,
                constants=dict(Traces=freeze(traces), Mutation="none"), expected=expected)


def controls(trace):
    tests = []

    def modified(name, invariant, edit):
        altered = copy.deepcopy(trace)
        edit(altered)
        tests.append(case(name, [altered], invariant, "mutation"))

    modified("disabled-child", "OnlyAvailableTouched", lambda t: t["available"].remove(1))
    modified("wrong-snapshot", "SnapshotsMatchC", lambda t: t["events"][0]["listed"].append(1))
    modified("missing-unregister", "CleanupSafe", lambda t: t["events"].__delitem__(
        next(i for i, e in enumerate(t["events"]) if e["kind"] == "unregister")))
    modified("missing-unlink", "FinalOwnership", lambda t: t["events"].__delitem__(
        next(i for i, e in enumerate(t["events"]) if e["kind"] == "unlink")))
    modified("duplicate-link", "ProtocolValid", lambda t: t["events"].insert(3, copy.deepcopy(t["events"][2])))

    def early(t):
        i = next(i for i, e in enumerate(t["events"]) if e["kind"] == "register-ok")
        t["events"].insert(3, t["events"].pop(i))
    modified("early-registration", "AllScannedBeforePublish", early)

    def incomplete(t):
        t["available"].append(3)
        t["children"] = 3
    modified("incomplete-success", "SuccessComplete", incomplete)
    return tests


def extend_probe_trace(modules, add, mutation):
    modules["NandProbeTrace"] = MODEL

    def event(kind, chip=0, listed=(), published=()):
        return dict(kind=kind, chip=chip, listed=list(listed), published=list(published))

    good = dict(available=[1, 2], events=[
        event("scan-start", 1), event("scan-ok", 1), event("link", 1, (1,)),
        event("scan-start", 2, (1,)), event("scan-ok", 2, (1,)), event("link", 2, (1, 2)),
        event("register-ok", 1, (1, 2), (1,)), event("register-ok", 2, (1, 2), (1, 2)),
        event("probe-ok", 0, (1, 2), (1, 2)), event("unregister", 1, (1, 2), (2,)),
        event("unlink", 1, (2,), (2,)), event("unregister", 2, (2,)),
        event("unlink", 2), event("finish")])
    scan_error = dict(available=[1, 2], events=good["events"][:4] + [
        event("scan-error", 2, (1,)), event("unlink", 1), event("probe-error"), event("finish")])
    register_error = dict(available=[1, 2], events=good["events"][:7] + [
        event("register-error", 2, (1, 2), (1,)), event("unregister", 1, (1, 2)),
        event("unlink", 1, (2,)), event("unlink", 2), event("probe-error"), event("finish")])
    empty = dict(available=[], events=[event("probe-ok"), event("finish")])
    disabled = dict(available=[2], events=[
        event("scan-start", 2), event("scan-ok", 2), event("link", 2, (2,)),
        event("register-ok", 2, (2,), (2,)), event("probe-ok", 0, (2,), (2,)),
        event("unregister", 2, (2,)), event("unlink", 2), event("finish")])
    tests = [case("selftest-" + name, [trace]) for name, trace in (
        ("success", good), ("scan-error", scan_error), ("register-error", register_error),
        ("empty", empty), ("disabled", disabled))] + controls(good)
    for test in tests:
        add(test["module"], test["name"], test["constants"],
            expected=test["expected"], kind=test["kind"])
