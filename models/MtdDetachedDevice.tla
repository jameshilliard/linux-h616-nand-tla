------------------------ MODULE MtdDetachedDevice ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Abstract reference budget, not a C/LKMM refinement proof. Callback
\* draining at RemoveInterface is checked separately by NvmemDisconnect.
\* One open leaf handle retains every ancestor; a hidden concatenation
\* component can retain leaf metadata independently of class visibility.
EXTENDS Integers, FiniteSets
CONSTANTS Depth, FailInit, KeepParentData, EmbedDevice, LateCoreHook, PinAPI, ClassBuiltIn
VARIABLE s
vars == <<s>>
Ids == 1..Depth
Init == s = [unbound |-> FailInit, handle |-> ~FailInit,
             hidden |-> TRUE, visible |-> Ids, data |-> Ids,
             bare |-> Ids, devices |-> Ids, queued |-> {},
             core |-> TRUE, provider |-> TRUE, context |-> TRUE,
             api |-> FALSE, badRelease |-> FALSE,
             classRemoved |-> FALSE, classReleased |-> FALSE, badClass |-> FALSE]

Unbind == /\ ~s.unbound
          /\ s' = [s EXCEPT !.unbound = TRUE]
Close == /\ s.handle
         /\ s' = [s EXCEPT !.handle = FALSE]
DropHidden == /\ s.unbound /\ s.hidden
              /\ s' = [s EXCEPT !.hidden = FALSE]
RemoveInterface(i) == /\ s.unbound /\ ~s.handle /\ i \in s.visible
    /\ (i = Depth \/ i + 1 \notin s.visible)
    /\ s' = [s EXCEPT !.visible = @ \ {i}]
ReleaseData(i) == /\ s.core /\ ~s.api /\ i \in s.data /\ i \notin s.visible
    /\ (i # Depth \/ ~s.hidden)
    /\ (~KeepParentData \/ i = Depth \/ i + 1 \notin s.data)
    /\ s' = [s EXCEPT !.data = @ \ {i}, !.api = TRUE,
                      !.devices = IF EmbedDevice THEN @ \ {i} ELSE @]
ReturnAPI == /\ s.api
             /\ s' = [s EXCEPT !.api = FALSE]
FreeContext == /\ s.unbound /\ s.data = {} /\ s.context
               /\ s' = [s EXCEPT !.context = FALSE]
UnloadProvider == /\ ~s.context /\ s.provider
                  /\ s' = [s EXCEPT !.provider = FALSE]
ClassUnregister == /\ ~s.provider /\ s.data = {} /\ ~s.classRemoved
    /\ (~PinAPI \/ ~s.api)
    /\ s' = [s EXCEPT !.classRemoved = TRUE]
UnloadCore == /\ ~s.provider /\ s.data = {} /\ s.core /\ s.classRemoved
              /\ (~PinAPI \/ ~s.api)
              /\ s' = [s EXCEPT !.core = FALSE]
ClassRelease == /\ s.classRemoved /\ ~s.classReleased
    /\ s' = [s EXCEPT !.classReleased = TRUE,
                      !.badClass = ~ClassBuiltIn /\ ~s.core]
BarePut(i) == /\ s.unbound /\ i \in s.bare
              /\ s' = [s EXCEPT !.bare = @ \ {i}]
QueueRelease(i) == /\ i \in s.devices /\ i \notin s.queued
    /\ i \notin s.bare /\ i \notin s.data
    /\ s' = [s EXCEPT !.queued = @ \cup {i}]
DeviceRelease(i) == /\ i \in s.queued
    /\ s' = [s EXCEPT !.queued = @ \ {i}, !.devices = @ \ {i},
                      !.badRelease = @ \/ (LateCoreHook /\ ~s.core)]
Finished == ~s.core /\ s.devices = {} /\ s.classReleased
Done == Finished /\ UNCHANGED vars
Next == Unbind \/ Close \/ DropHidden \/ ReturnAPI \/ FreeContext
        \/ UnloadProvider \/ ClassUnregister \/ UnloadCore \/ ClassRelease
        \/ (\E i \in Ids : RemoveInterface(i) \/ ReleaseData(i) \/ BarePut(i)
                           \/ QueueRelease(i) \/ DeviceRelease(i)) \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK == /\ s.visible \subseteq Ids /\ s.data \subseteq Ids
          /\ s.bare \subseteq Ids /\ s.devices \subseteq Ids
          /\ s.queued \subseteq Ids
DeviceStorage == (s.data \cup s.bare \cup s.queued) \subseteq s.devices
ParentData == \A i \in 2..Depth : i \in s.data => i - 1 \in s.data
ProviderStorage == s.data # {} => s.context /\ s.provider /\ s.core
CoreOnlyRelease == ~s.badRelease
APIResident == s.api => s.core
ClassStorage == ~s.badClass
Terminates == <>Finished
===========================================================================
