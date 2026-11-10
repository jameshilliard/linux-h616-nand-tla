--------------------------- MODULE MtdPMDetach ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* A queued system-sleep callback owns a device reference, not an MTD handle.
\* device_del() takes device_lock() initially, but releases it before removing
\* the device from the PM list. Selection and execution of MTD's class suspend
\* and resume callbacks both hold device_lock(). Model that mutex contract,
\* not the PM scheduler or the NAND driver's own hardware disconnection.
EXTENDS Integers, FiniteSets
CONSTANTS Tasks, DrainUnderLock, ClearPointers
VARIABLE s
vars == <<s>>
Ids == 1..Tasks
Init == s = [phase |-> "live", queued |-> {}, used |-> {}, active |-> 0,
             attached |-> TRUE, data |-> TRUE, core |-> TRUE, device |-> TRUE]

Queue(i) == /\ i \in Ids \ s.used
    /\ s.phase \in {"live", "removing"}
    /\ s' = [s EXCEPT !.used = @ \cup {i}, !.queued = @ \cup {i}]
BeginRemove == /\ s.phase = "live" /\ s.active = 0
    /\ s' = [s EXCEPT !.phase = "removing"]
DeviceDel == /\ s.phase = "removing"
    /\ s' = [s EXCEPT !.phase = "deleted"]
EnterPM(i) == /\ i \in s.queued /\ s.active = 0
    /\ s' = [s EXCEPT !.queued = @ \ {i},
                      !.active = IF s.attached THEN i ELSE 0]
ReturnPM == /\ s.active # 0
    /\ s' = [s EXCEPT !.active = 0]
Detach == /\ s.phase = "deleted"
    /\ (~DrainUnderLock \/ s.active = 0)
    /\ s' = [s EXCEPT !.phase = "detached",
                      !.attached = IF ClearPointers THEN FALSE ELSE @]
ReleaseData == /\ s.phase = "detached"
    /\ s' = [s EXCEPT !.phase = "freed", !.data = FALSE]
UnloadCore == /\ ~s.data /\ s.core
    /\ s' = [s EXCEPT !.core = FALSE]
FinalDevicePut == /\ ~s.data /\ s.queued = {} /\ s.active = 0 /\ s.device
    /\ s' = [s EXCEPT !.device = FALSE]
Finished == ~s.device /\ ~s.core
Done == Finished /\ UNCHANGED vars
Next == BeginRemove \/ DeviceDel \/ ReturnPM \/ Detach \/ ReleaseData
        \/ UnloadCore \/ FinalDevicePut
        \/ (\E i \in Ids : Queue(i) \/ EnterPM(i)) \/ Done
Spec == Init /\ [][Next]_vars
TypeOK == /\ s.phase \in {"live", "removing", "deleted", "detached", "freed"}
          /\ s.queued \subseteq s.used /\ s.used \subseteq Ids
          /\ s.active \in {0} \cup Ids
          /\ s.attached \in BOOLEAN /\ s.data \in BOOLEAN
          /\ s.core \in BOOLEAN /\ s.device \in BOOLEAN
MetadataAlive == s.active # 0 => s.data
CallbackResident == s.active # 0 => s.core
DeviceAlive == (s.data \/ s.queued # {} \/ s.active # 0) => s.device
===========================================================================
