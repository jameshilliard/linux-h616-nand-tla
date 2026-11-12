----------------------- MODULE NvmemCellPublication -----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Bounded sysfs rebuild, failed registration and layout-rebind contracts.
\* The C allocation fault matrix and QEMU exercise implementations separately.
EXTENDS Naturals, FiniteSets
CONSTANTS Refresh, ExplicitLayout, RemoveBeforeFree, PairedNotifier, ExplicitOwner
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "provider", cells |-> {"base"}, attrs |-> {"base"},
    retired |-> {}, lateOwned |-> FALSE, active |-> FALSE,
    attrStorage |-> TRUE, registered |-> FALSE, notified |-> FALSE,
    error |-> FALSE, badAttrs |-> FALSE, badCell |-> FALSE, badNotify |-> FALSE,
    pinnedController |-> FALSE]
Bind == /\ s.pc = "provider"
    /\ s' = [s EXCEPT !.pc = "late", !.cells = @ \cup {"layout"},
                      !.attrs = IF Refresh THEN @ \cup {"layout"} ELSE @,
                      !.badAttrs = ~Refresh]
AddLate == /\ s.pc = "late"
    /\ s' = [s EXCEPT !.pc = "unbind", !.cells = @ \cup {"late"},
                      !.lateOwned = ~ExplicitLayout]
Unbind == /\ s.pc = "unbind"
    /\ s' = [s EXCEPT !.pc = "rebind",
       !.cells = IF s.lateOwned THEN {"base"} ELSE {"base", "late"},
       !.attrs = IF s.lateOwned THEN {"base"} ELSE {"base", "late"},
       !.badCell = s.lateOwned, !.retired = {"layout"}]
Rebind == /\ s.pc = "rebind"
    /\ s' = [s EXCEPT !.pc = "publish", !.cells = @ \cup {"replacement"},
                      !.attrs = IF Refresh THEN @ \cup {"replacement"} ELSE @,
                      !.badAttrs = @ \/ ~Refresh]
Publish == /\ s.pc = "publish"
    /\ \E success \in BOOLEAN: s' = [s EXCEPT !.pc = "reader",
        !.registered = success, !.notified = success, !.error = ~success,
        !.pinnedController = ~ExplicitOwner]
Reader == /\ s.pc = "reader"
    /\ s' = [s EXCEPT !.pc = "remove", !.active = TRUE]
Remove == /\ s.pc = "remove"
    /\ s' = [s EXCEPT !.pc = "drain",
        !.badNotify = ~s.notified /\ ~PairedNotifier,
        !.attrStorage = RemoveBeforeFree]
Drain == /\ s.pc = "drain"
    /\ s' = [s EXCEPT !.pc = "free", !.active = FALSE, !.attrs = {}]
Free == /\ s.pc = "free"
    /\ s' = [s EXCEPT !.pc = "done", !.attrStorage = FALSE]
Finished == s.pc = "done"
Done == Finished /\ UNCHANGED vars
Next == Bind \/ AddLate \/ Unbind \/ Rebind \/ Publish \/ Reader \/ Remove \/ Drain \/ Free \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
AllCellAttributes == ~s.badAttrs
ProviderCellsSurvive == ~s.badCell
ActiveAttributesAlive == s.active => s.attrStorage
NotificationsPaired == ~s.badNotify
ControllerNotPinned == ~s.pinnedController
Terminates == <>Finished
=============================================================================
