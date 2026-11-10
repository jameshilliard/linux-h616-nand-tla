------------------------- MODULE NvmemDisconnect -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Provider code/context and core-owned retained data have separate lifetimes.
\* SRCU entry, callback return and read-side exit are distinct transitions.
\* This assumes the SRCU grace-period contract, not a C/LKMM refinement proof.
EXTENDS Integers, FiniteSets
CONSTANTS Readers, DrainCallbacks, ProtectPostprocess, RejectLate, LateReleaseHook
VARIABLE s
vars == <<s>>
Ids == 1..Readers
Phases == {"idle", "entry", "callback", "post_pending", "post", "return", "done", "closed"}
Init == s = [owner |-> "live", phase |-> [i \in Ids |-> "idle"],
             active |-> {}, registration |-> TRUE, bare |-> TRUE,
             storage |-> TRUE, badRelease |-> FALSE]

Enter(i) == /\ s.phase[i] = "idle"
    /\ s' = [s EXCEPT !.phase[i] = "entry", !.active = @ \cup {i}]
Check(i) == /\ s.phase[i] = "entry"
    /\ s' = [s EXCEPT !.phase[i] = IF s.owner = "live" \/ ~RejectLate
                                 THEN "callback" ELSE "return"]
CallbackReturn(i) == /\ s.phase[i] = "callback"
    /\ s' = [s EXCEPT !.phase[i] = "post_pending",
                      !.active = IF ProtectPostprocess THEN @ ELSE @ \ {i}]
PostStart(i) == /\ s.phase[i] = "post_pending"
    /\ s' = [s EXCEPT !.phase[i] = "post"]
PostReturn(i) == /\ s.phase[i] = "post"
    /\ s' = [s EXCEPT !.phase[i] = "return"]
Exit(i) == /\ s.phase[i] = "return"
    /\ s' = [s EXCEPT !.phase[i] = "done", !.active = @ \ {i}]
Put(i) == /\ s.phase[i] = "done"
    /\ s' = [s EXCEPT !.phase[i] = "closed"]
Disconnect == /\ s.owner = "live"
    /\ s' = [s EXCEPT !.owner = "closing"]
Drain == /\ s.owner = "closing" /\ (~DrainCallbacks \/ s.active = {})
    /\ s' = [s EXCEPT !.owner = "drained"]
FreeContext == /\ s.owner = "drained"
    /\ s' = [s EXCEPT !.owner = "freed", !.registration = FALSE]
Unload == /\ s.owner = "freed"
    /\ s' = [s EXCEPT !.owner = "unloaded"]
BarePut == /\ s.bare /\ s.owner = "unloaded"
    /\ s' = [s EXCEPT !.bare = FALSE]
DeviceRelease == /\ s.storage /\ ~s.registration /\ ~s.bare
    /\ \A i \in Ids : s.phase[i] = "closed"
    /\ s' = [s EXCEPT !.storage = FALSE,
                      !.badRelease = LateReleaseHook /\ s.owner = "unloaded"]
Finished == ~s.storage /\ s.owner = "unloaded"
Done == Finished /\ UNCHANGED vars
Next == (\E i \in Ids : Enter(i) \/ Check(i) \/ CallbackReturn(i) \/ PostStart(i)
                     \/ PostReturn(i) \/ Exit(i) \/ Put(i))
        \/ Disconnect \/ Drain \/ FreeContext \/ Unload \/ BarePut \/ DeviceRelease \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK == /\ s.phase \in [Ids -> Phases] /\ s.active \subseteq Ids
          /\ s.owner \in {"live", "closing", "drained", "freed", "unloaded"}
CallbackStorage == (\E i \in Ids : s.phase[i] \in {"callback", "post"})
                   => s.owner \in {"live", "closing"}
CoreStorage == (s.registration \/ s.bare \/ (\E i \in Ids : s.phase[i] # "closed"))
               => s.storage
CoreOnlyRelease == ~s.badRelease
Terminates == <>Finished
=============================================================================
