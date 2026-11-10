------------------------ MODULE DeviceReleaseOwner ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Split callback return from the core's final module_put(). Device memory
\* can be freed inside the callback; the core caches the owner first.
\* The constructing caller pins the module until it hands off its devices.
\* Forced removal and failed module initialization with live devices are
\* outside the opt-in device API contract.
EXTENDS Integers, FiniteSets
CONSTANTS Devices, Outcome, PinAtInit, PutAfterReturn, CacheOwner
VARIABLE s
vars == <<s>>
Ids == 1..Devices
Stages == {"new", "initialized", "held", "queued", "callback",
           "epilogue", "returned", "done"}
Init == s = [stage |-> [i \in Ids |-> "new"],
             pins |-> {}, cached |-> {}, storage |-> Ids,
             caller |-> TRUE, loaded |-> TRUE, staleRead |-> FALSE]
Initialize(i) == /\ s.caller /\ s.stage[i] = "new"
    /\ s' = [s EXCEPT !.stage[i] = "initialized",
                      !.pins = IF PinAtInit THEN @ \cup {i} ELSE @]
Prepare(i) == /\ s.stage[i] = "initialized"
    /\ s' = [s EXCEPT !.stage[i] = "held",
                      !.pins = IF ~PinAtInit /\ Outcome = "registered"
                               THEN @ \cup {i} ELSE @]
Handoff == /\ s.caller /\ \A i \in Ids : s.stage[i] \notin {"new", "initialized"}
    /\ s' = [s EXCEPT !.caller = FALSE]
LastPut(i) == /\ s.stage[i] = "held"
    /\ s' = [s EXCEPT !.stage[i] = "queued"]
Enter(i) == /\ s.stage[i] = "queued"
    /\ s' = [s EXCEPT !.stage[i] = "callback",
                      !.cached = IF CacheOwner THEN @ \cup {i} ELSE @]
CallbackBody(i) == /\ s.stage[i] = "callback"
    /\ s' = [s EXCEPT !.stage[i] = "epilogue", !.storage = @ \ {i},
                      !.pins = IF PutAfterReturn THEN @ ELSE @ \ {i}]
Return(i) == /\ s.stage[i] = "epilogue"
    /\ s' = [s EXCEPT !.stage[i] = "returned"]
CorePut(i) == /\ s.stage[i] = "returned"
    /\ s' = [s EXCEPT !.stage[i] = "done", !.pins = @ \ {i},
                      !.staleRead = @ \/ (PutAfterReturn /\ i \notin s.cached)]
Unload == /\ ~s.caller /\ s.pins = {} /\ s.loaded
    /\ s' = [s EXCEPT !.loaded = FALSE]
Finish == ~s.caller /\ ~s.loaded /\ \A i \in Ids : s.stage[i] = "done"
Done == Finish /\ UNCHANGED vars
Next == (\E i \in Ids : Initialize(i) \/ Prepare(i) \/ LastPut(i) \/ Enter(i)
                    \/ CallbackBody(i) \/ Return(i) \/ CorePut(i)) \/ Handoff \/ Unload \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK == /\ s.stage \in [Ids -> Stages] /\ s.pins \subseteq Ids
          /\ s.cached \subseteq Ids /\ s.storage \subseteq Ids
CallbackCode == (\E i \in Ids : s.stage[i] \in {"callback", "epilogue"}) => s.loaded
PendingCode == (\E i \in Ids : s.stage[i] \in {"held", "queued"}) => s.loaded
CachedOwner == ~s.staleRead
NoLeaks == (\A i \in Ids : s.stage[i] = "done") => s.pins = {}
Terminates == <>Finish
=============================================================================
