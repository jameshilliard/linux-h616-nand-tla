--------------------------- MODULE NandFdLocking ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Source lock order: nand_get_device locks chip then shared controller;
\* nand_release_device unlocks controller then chip. RW close calls nand_sync
\* before put_mtd_device. Gate policies are hypothetical, not a source fix.
EXTENDS Integers, FiniteSets
CONSTANTS SameChip, Policy, GateClose, Mutation
VARIABLE s
vars == <<s>>
Users == 1..2
Chips == 1..2
Chip(u) == IF SameChip THEN 1 ELSE u
Init == s=[pc |-> [u \in Users |-> "read"], refs |-> Users,
    chipLock |-> [c \in Chips |-> 0], controller |-> 0, admitted |-> {},
    online |-> TRUE, resources |-> TRUE, removed |-> FALSE,
    bad |-> FALSE, overlap |-> FALSE, active |-> {}]
Enter(u) == /\ s.pc[u] \in {"read","close"}
    /\ LET gated == Policy="gate" /\ (s.pc[u]="read" \/ GateClose)
           denied == gated /\ ~s.online
       IN s'=[s EXCEPT !.pc[u]=IF denied THEN (IF @="read" THEN "close" ELSE "put")
                    ELSE IF @="read" THEN "read-chip" ELSE "close-chip",
           !.admitted=IF denied THEN @ ELSE @ \cup {u}]
ChipLock(u) == /\ s.pc[u] \in {"read-chip","close-chip"} /\ s.chipLock[Chip(u)]=0
    /\ s'=[s EXCEPT !.chipLock[Chip(u)]=u, !.bad=@ \/ ~s.resources,
         !.pc[u]=IF @="read-chip" THEN "read-controller" ELSE "close-controller"]
ControllerLock(u) == /\ s.pc[u] \in {"read-controller","close-controller"}
    /\ (s.controller=0 \/ Mutation="skip-controller-lock")
    /\ s'=[s EXCEPT !.controller=u,!.active=@ \cup {u},!.bad=@ \/ ~s.resources,
         !.overlap=@ \/ s.active # {},
         !.pc[u]=IF @="read-controller" THEN "read-unlock" ELSE "close-unlock"]
ControllerUnlock(u) == /\ s.pc[u] \in {"read-unlock","close-unlock"}
    /\ s'=[s EXCEPT !.controller=0,!.active=@ \ {u},!.bad=@ \/ ~s.resources,
         !.pc[u]=IF @="read-unlock" THEN "read-release" ELSE "close-release"]
ChipUnlock(u) == /\ s.pc[u] \in {"read-release","close-release"}
    /\ s'=[s EXCEPT !.chipLock[Chip(u)]=0,!.admitted=@ \ {u},!.bad=@ \/ ~s.resources,
         !.pc[u]=IF @="read-release" THEN "close" ELSE "put"]
Put(u) == /\ s.pc[u]="put"
    /\ s'=[s EXCEPT !.refs=@ \ {u},!.pc[u]="done"]
Unbind == /\ s.online
    /\ s'=[s EXCEPT !.online=FALSE]
Cleanup == /\ ~s.online /\ ~s.removed
    /\ (Policy # "gate" \/ s.admitted={})
    /\ (Policy # "last-ref" \/ s.refs={})
    /\ s'=[s EXCEPT !.resources=FALSE,!.removed=TRUE,!.bad=@ \/ s.admitted # {}]
Finish == s.removed /\ \A u \in Users:s.pc[u]="done"
Done == Finish /\ UNCHANGED vars
UserStep == \E u \in Users:Enter(u) \/ ChipLock(u) \/ ControllerLock(u) \/ ControllerUnlock(u) \/ ChipUnlock(u) \/ Put(u)
Next == UserStep \/ Unbind \/ Cleanup \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next) /\ WF_vars(UserStep)
LiveLockStorage == ~s.bad
OneControllerOwner == ~s.overlap /\ Cardinality(s.active)<=1
CompleteRelease == Finish => s.refs={} /\ s.admitted={} /\ s.controller=0 /\ \A c \in Chips:s.chipLock[c]=0
Terminates == <>Finish
=============================================================================
