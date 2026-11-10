------------------------ MODULE CallbackRetirement ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Two generations of callback-backed metadata, as used by layout cells and
\* virtual concatenations. SRCU is modeled by its grace-period contract.
EXTENDS Integers, FiniteSets
CONSTANTS Readers, KeepHandles, Drain, CheckRetired, IdentityGate
VARIABLE s
vars == <<s>>
Ids == 1..Readers
Gens == 1..2
Init == s = [current |-> 1,
    owner |-> [g \in Gens |-> IF g = 1 THEN "live" ELSE "absent"],
    meta |-> [g \in Gens |-> g = 1],
    handle |-> [i \in Ids |-> 0], phase |-> [i \in Ids |-> "new"], active |-> {}]
Acquire(i) == /\ s.phase[i] = "new" /\ s.current # 0
    /\ s' = [s EXCEPT !.handle[i] = s.current, !.phase[i] = "held"]
Enter(i) == /\ s.phase[i] = "held"
    /\ s' = [s EXCEPT !.phase[i] = "entry", !.active = @ \cup {i}]
Check(i) == /\ s.phase[i] = "entry"
    /\ s' = [s EXCEPT !.phase[i] =
        IF ~CheckRetired \/ (IF IdentityGate THEN s.owner[s.handle[i]] = "live"
                             ELSE s.current # 0)
        THEN "callback" ELSE "return"]
CallbackReturn(i) == /\ s.phase[i] = "callback"
    /\ s' = [s EXCEPT !.phase[i] = "return"]
Exit(i) == /\ s.phase[i] = "return"
    /\ s' = [s EXCEPT !.phase[i] = "done", !.active = @ \ {i}]
Put(i) == /\ s.phase[i] \in {"held", "done"}
    /\ s' = [s EXCEPT !.phase[i] = "closed", !.handle[i] = 0]
Disconnect(g) == /\ s.owner[g] = "live"
    /\ s' = [s EXCEPT !.owner[g] = "closing", !.current = 0]
DrainCallbacks(g) == /\ s.owner[g] = "closing"
    /\ (~Drain \/ {i \in s.active : s.handle[i] = g} = {})
    /\ s' = [s EXCEPT !.owner[g] = "drained"]
ReleaseProvider(g) == /\ s.owner[g] = "drained"
    /\ s' = [s EXCEPT !.owner[g] = "freed"]
FreeMetadata(g) == /\ s.owner[g] = "freed" /\ s.meta[g]
    /\ (~KeepHandles \/ \A i \in Ids : s.handle[i] # g)
    /\ s' = [s EXCEPT !.meta[g] = FALSE]
Rebind == /\ s.current = 0 /\ s.owner[1] # "live" /\ s.owner[2] = "absent"
    /\ s' = [s EXCEPT !.current = 2, !.owner[2] = "live", !.meta[2] = TRUE]
Done == UNCHANGED vars
Next == (\E i \in Ids : Acquire(i) \/ Enter(i) \/ Check(i) \/ CallbackReturn(i)
                     \/ Exit(i) \/ Put(i))
        \/ (\E g \in Gens : Disconnect(g) \/ DrainCallbacks(g)
                          \/ ReleaseProvider(g) \/ FreeMetadata(g)) \/ Rebind \/ Done
Spec == Init /\ [][Next]_vars
TypeOK == /\ s.handle \in [Ids -> 0..2] /\ s.active \subseteq Ids
          /\ s.meta \in [Gens -> BOOLEAN]
MetadataAlive == \A i \in Ids : s.handle[i] # 0 => s.meta[s.handle[i]]
CallbackAlive == \A i \in Ids : s.phase[i] = "callback" =>
                               s.owner[s.handle[i]] \in {"live", "closing"}
=============================================================================
