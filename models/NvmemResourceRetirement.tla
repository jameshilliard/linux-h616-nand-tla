---------------------- MODULE NvmemResourceRetirement ----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Existing module ownership protects legacy managed-provider unload. A
\* provider which unregisters explicitly can drain before freeing context.
\* Explicit sysfs unbind of a legacy provider is NOT protected by module pins
\* and is outside this model's safety claim. No weak-memory proof is implied.
EXTENDS Naturals
CONSTANTS Managed, KeepModulePins, ReleaseGPIOEarly
VARIABLE s
vars == <<s>>

Init == s = [stage |-> "live", reader |-> "idle", handle |-> TRUE,
             bare |-> TRUE, active |-> FALSE, disconnected |-> FALSE,
             context |-> TRUE, storage |-> TRUE, gpio |-> TRUE,
             reprobed |-> FALSE, busy |-> FALSE]

Enter == /\ s.handle /\ s.reader = "idle"
    /\ s' = [s EXCEPT !.reader = "entered", !.active = TRUE]
Check == /\ s.reader = "entered"
    /\ s' = [s EXCEPT !.reader = IF s.disconnected THEN "return" ELSE "callback"]
Return == /\ s.reader = "callback"
    /\ s' = [s EXCEPT !.reader = "return"]
Exit == /\ s.reader = "return"
    /\ s' = [s EXCEPT !.reader = "done", !.active = FALSE]
Put == /\ s.handle /\ s.reader \in {"idle", "done"}
    /\ s' = [s EXCEPT !.handle = FALSE]

BeginRemove == /\ s.stage = "live"
    /\ (~Managed \/ ~KeepModulePins \/ ~s.handle)
    /\ s' = [s EXCEPT !.stage = "remove"]
ManagedCleanup == /\ Managed /\ s.stage = "remove"
    /\ s' = [s EXCEPT !.stage = "devres", !.context = FALSE]
Unregister == /\ s.stage = (IF Managed THEN "devres" ELSE "remove")
    /\ s' = [s EXCEPT !.stage = "closing", !.disconnected = TRUE]
Drain == /\ s.stage = "closing" /\ ~s.active
    /\ s' = [s EXCEPT !.stage = "drained",
                      !.gpio = IF ReleaseGPIOEarly THEN FALSE ELSE @]
FinishRemove == /\ s.stage = "drained"
    /\ s' = [s EXCEPT !.stage = "gone", !.context = FALSE]
Reprobe == /\ s.stage = "gone" /\ ~s.reprobed
    /\ s' = [s EXCEPT !.reprobed = TRUE, !.busy = s.gpio]
BarePut == /\ s.reprobed /\ s.bare
    /\ s' = [s EXCEPT !.bare = FALSE]
FinalRelease == /\ s.stage = "gone" /\ ~s.handle /\ ~s.bare /\ s.storage
    /\ s' = [s EXCEPT !.storage = FALSE, !.gpio = FALSE]

Finished == ~s.storage
Done == Finished /\ UNCHANGED vars
Next == Enter \/ Check \/ Return \/ Exit \/ Put \/ BeginRemove \/ ManagedCleanup
        \/ Unregister \/ Drain \/ FinishRemove \/ Reprobe \/ BarePut \/ FinalRelease \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)

TypeOK == /\ s.stage \in {"live", "remove", "devres", "closing", "drained", "gone"}
          /\ s.reader \in {"idle", "entered", "callback", "return", "done"}
          /\ s.active \in BOOLEAN /\ s.gpio \in BOOLEAN
CallbackStorage == s.reader = "callback" => s.context
RetainedStorage == (s.handle \/ s.bare) => s.storage
ReprobeAvailable == ~s.busy
Terminates == <>Finished
=============================================================================
