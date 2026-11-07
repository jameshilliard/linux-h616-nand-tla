------------------------- MODULE NandRetainedLifetime -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Candidate contract: retained lock/metadata storage, disconnected hardware,
\* per-handle plus per-device references, and suspend wakeup on disconnect.
EXTENDS Integers, FiniteSets
CONSTANTS DeviceCount, InitiallySuspended, Drain, RetainDevices, RetainParents, Wake
VARIABLE s
vars == <<s>>
Users == 1..2
Init == s=[pc |-> [u \in Users |-> "open"], refs |-> {}, chip |-> 0,
    controller |-> 0, active |-> {}, waiting |-> {}, disconnected |-> FALSE,
    published |-> TRUE, hardware |-> TRUE, metadata |-> TRUE, driver |-> TRUE,
    devices |-> 1..DeviceCount, remove |-> 0, badHardware |-> FALSE,
    badStorage |-> FALSE]
Open(u) == /\ s.pc[u]="open"
    /\ LET ok == s.published /\ ~s.disconnected
       IN s'=[s EXCEPT !.pc[u]=IF ok THEN "read" ELSE "done",
                     !.refs=IF ok THEN @ \cup {u} ELSE @]
ChipLock(u) == /\ s.pc[u] \in {"read","close"} /\ s.chip=0
    /\ s'=[s EXCEPT !.chip=u, !.badStorage=@ \/ ~s.metadata,
         !.pc[u]=IF @="read" THEN "read-check" ELSE "close-check"]
Check(u) == /\ s.pc[u] \in {"read-check","close-check"}
    /\ s'=IF s.disconnected
          THEN [s EXCEPT !.chip=0,!.pc[u]=IF @="read-check" THEN "close" ELSE "put"]
          ELSE IF InitiallySuspended
          THEN [s EXCEPT !.chip=0,!.waiting=@ \cup {u},
                        !.pc[u]=IF @="read-check" THEN "read-wait" ELSE "close-wait"]
          ELSE [s EXCEPT !.pc[u]=IF @="read-check" THEN "read-controller" ELSE "close-controller"]
WakeWaiter(u) == /\ s.pc[u] \in {"read-wait","close-wait"} /\ u \notin s.waiting
    /\ s'=[s EXCEPT !.pc[u]=IF @="read-wait" THEN "read" ELSE "close"]
ControllerLock(u) == /\ s.pc[u] \in {"read-controller","close-controller"} /\ s.controller=0
    /\ s'=[s EXCEPT !.controller=u,!.active=@ \cup {u},
         !.badHardware=@ \/ (~s.hardware /\ s.pc[u]="read-controller"),
         !.badStorage=@ \/ ~s.metadata,
         !.pc[u]=IF @="read-controller" THEN "read-unlock" ELSE "close-unlock"]
Unlock(u) == /\ s.pc[u] \in {"read-unlock","close-unlock"}
    /\ s'=[s EXCEPT !.chip=0,!.controller=0,!.active=@ \ {u},
         !.badHardware=@ \/ (~s.hardware /\ s.pc[u]="read-unlock"),
         !.badStorage=@ \/ ~s.metadata,
         !.pc[u]=IF @="read-unlock" THEN "close" ELSE "put"]
Put(u) == /\ s.pc[u]="put"
    /\ s'=[s EXCEPT !.refs=@ \ {u},!.pc[u]="done",!.badStorage=@ \/ ~s.metadata]
Disconnect == /\ s.remove=1 /\ (~Drain \/ (s.chip=0 /\ s.controller=0))
    /\ s'=[s EXCEPT !.disconnected=TRUE,!.remove=2,
                     !.waiting=IF Wake THEN {} ELSE @]
Unpublish == /\ s.remove=0
    /\ s'=[s EXCEPT !.published=FALSE,!.remove=1]
DropInitial == /\ s.remove=2
    /\ s'=[s EXCEPT !.driver=FALSE,!.remove=3]
Teardown == /\ s.remove=3
    /\ s'=[s EXCEPT !.hardware=FALSE,!.remove=4,
         !.badHardware=@ \/ (\E u \in s.active:s.pc[u]="read-unlock")]
\* Device 1 embeds the master, retained separately by life_refcnt. For deeper
\* partitions the child holds its parent's device reference through release.
DeviceRelease == \E d \in s.devices:
    /\ ~s.published /\ s.refs={}
    /\ (~RetainParents \/ d=1 \/ (d+1) \notin s.devices)
    /\ s'=[s EXCEPT !.devices=@ \ {d},!.badStorage=@ \/ ~s.metadata]
MetadataRelease == /\ s.metadata /\ ~s.driver /\ s.refs={}
    /\ (~RetainDevices \/ s.devices={})
    /\ s'=[s EXCEPT !.metadata=FALSE]
Finish == s.remove=4 /\ ~s.metadata /\ s.devices={} /\ \A u \in Users:s.pc[u]="done"
Done == Finish /\ UNCHANGED vars
Idle == s.remove=4 /\ UNCHANGED vars
Next == (\E u \in Users: Open(u) \/ ChipLock(u) \/ Check(u) \/ WakeWaiter(u) \/
                         ControllerLock(u) \/ Unlock(u) \/ Put(u)) \/
        Disconnect \/ Unpublish \/ DropInitial \/ Teardown \/ DeviceRelease \/ MetadataRelease \/ Done \/ Idle
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
LiveStorage == ~s.badStorage /\ (~s.metadata => ~s.driver /\ s.refs={} /\ s.devices={})
ParentStorage == \A d \in s.devices: d>2 => (d-1) \in s.devices
DrainedHardware == ~s.badHardware
Exclusive == Cardinality(s.active)<=1
Terminates == <>Finish
=============================================================================
