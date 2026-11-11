----------------------- MODULE NandDataCodeLifetime -----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Core-owned storage outlives controller code. A markbad operation drops the
\* chip/controller locks between its BBM and BBT steps, but holds markbad_lock.
\* Ordinary MTD handle references keep the NAND core resident (assumed here).
\* Mutex, reference-count and waitqueue contracts are abstract, not LKMM proofs.
EXTENDS Integers, FiniteSets
CONSTANTS Users, Suspended, Drain, DrainMarkbad, CacheLayout, CoreRelease, Wake
VARIABLE s
vars == <<s>>
Ids == 1..Users
Init == s = [pc |-> [i \in Ids |-> "start"], refs |-> Ids,
    chip |-> 0, markbad |-> 0, waiters |-> {}, remove |-> "bound",
    disconnected |-> FALSE, hardware |-> TRUE, auxiliary |-> TRUE,
    driver |-> TRUE, initial |-> TRUE, data |-> TRUE,
    badCode |-> FALSE, badData |-> FALSE, badAux |-> FALSE, badHardware |-> FALSE]

\* User 1 marks a block; any other user performs one ordinary NAND operation.
Start(i) == /\ s.pc[i] = "start" /\ (i # 1 \/ s.markbad = 0)
    /\ s' = [s EXCEPT !.pc[i] = "lock", !.markbad = IF i = 1 THEN i ELSE @]
ChipLock(i) == /\ s.pc[i] = "lock" /\ s.chip = 0
    /\ s' = [s EXCEPT !.chip = i, !.pc[i] = "check",
                      !.badData = @ \/ ~s.data]
Check(i) == /\ s.pc[i] = "check"
    /\ s' = IF s.disconnected
       THEN [s EXCEPT !.chip = 0, !.pc[i] = "layout",
                     !.markbad = IF i = 1 THEN 0 ELSE @]
       ELSE IF Suspended
       THEN [s EXCEPT !.chip = 0, !.pc[i] = "wait", !.waiters = @ \cup {i}]
       ELSE [s EXCEPT !.pc[i] = "bbm"]
WakeWaiter(i) == /\ s.pc[i] = "wait" /\ i \notin s.waiters
    /\ s' = [s EXCEPT !.pc[i] = "lock"]
BBM(i) == /\ s.pc[i] = "bbm"
    /\ s' = [s EXCEPT !.pc[i] = IF i = 1 THEN "bbt" ELSE "layout", !.chip = 0,
         !.badCode = @ \/ ~s.driver, !.badHardware = @ \/ ~s.hardware,
         !.badAux = @ \/ ~s.auxiliary]
BBT(i) == /\ s.pc[i] = "bbt"
    /\ s' = [s EXCEPT !.pc[i] = "layout", !.markbad = 0,
                      !.badAux = @ \/ ~s.auxiliary]
Layout(i) == /\ s.pc[i] = "layout"
    /\ s' = [s EXCEPT !.pc[i] = "put", !.badData = @ \/ ~s.data,
         !.badCode = @ \/ (~CacheLayout /\ ~s.driver),
         !.badAux = @ \/ (~CacheLayout /\ ~s.auxiliary)]
Put(i) == /\ s.pc[i] = "put"
    /\ s' = [s EXCEPT !.pc[i] = "done", !.refs = @ \ {i},
                      !.badData = @ \/ ~s.data]

Unpublish == /\ s.remove = "bound"
    /\ s' = [s EXCEPT !.remove = "unpublished"]
Disconnect == /\ s.remove = "unpublished" /\ (~Drain \/ s.chip = 0)
    /\ s' = [s EXCEPT !.remove = "disconnected", !.disconnected = TRUE,
                      !.waiters = IF Wake THEN {} ELSE @]
Cleanup == /\ s.remove = "disconnected" /\ (~DrainMarkbad \/ s.markbad = 0)
    /\ s' = [s EXCEPT !.remove = "cleaned", !.auxiliary = FALSE,
                      !.badCode = @ \/ ~s.driver]
Teardown == /\ s.remove = "cleaned"
    /\ s' = [s EXCEPT !.remove = "removed", !.hardware = FALSE,
                      !.initial = FALSE]
Unload == /\ s.remove = "removed" /\ s.driver
    /\ s' = [s EXCEPT !.driver = FALSE]
FinalPut == /\ ~s.initial /\ s.refs = {} /\ s.data
    /\ s' = [s EXCEPT !.data = FALSE,
                      !.badCode = @ \/ (~CoreRelease /\ ~s.driver)]
UserStep(i) == Start(i) \/ ChipLock(i) \/ Check(i) \/ WakeWaiter(i) \/ BBM(i)
               \/ BBT(i) \/ Layout(i) \/ Put(i)
RemoveStep == Unpublish \/ Disconnect \/ Cleanup \/ Teardown \/ Unload
Idle == s.disconnected /\ UNCHANGED vars
Next == (\E i \in Ids: UserStep(i)) \/ RemoveStep \/ FinalPut \/ Idle
Spec == Init /\ [][Next]_vars /\ (\A i \in Ids: WF_vars(UserStep(i)))
        /\ WF_vars(RemoveStep) /\ WF_vars(FinalPut)
CodeResident == ~s.badCode
StorageAlive == ~s.badData /\ (~s.data => ~s.initial /\ s.refs = {})
AuxiliaryAlive == ~s.badAux
HardwareAlive == ~s.badHardware
Terminates == <>(~s.driver /\ ~s.data /\ \A i \in Ids: s.pc[i] = "done")
=============================================================================
