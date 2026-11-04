------------------------ MODULE NandDetachPolicy ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Two chips, master/partition references, split open lookup/acquisition,
\* I/O through an old fd and probe rollback. Candidate policies require a
\* common gate for every MTD entry point; they are not production fixes.
EXTENDS Integers, FiniteSets
CONSTANTS Policy, Partition, ProbeFailure, SameChip, Mutation
VARIABLE s
vars == <<s>>
Chips == 1..2
Users == 1..2
Chip(u) == IF SameChip THEN 1 ELSE u
Published == IF ProbeFailure THEN {1} ELSE Chips
Init == s = [pc |-> "live", registered |-> Published, resources |-> Chips,
    owners |-> Published, partitions |-> IF Partition THEN Published ELSE {},
    user |-> [u \in Users |-> "lookup"], active |-> {},
    badIO |-> FALSE, badOpen |-> FALSE, next |-> 1]
Refs(c) == (IF c \in s.owners THEN 1 ELSE 0) +
    (IF c \in s.partitions THEN 1 ELSE 0) +
    Cardinality({u \in Users : Chip(u) = c /\ s.user[u] \in {"open", "io", "done"}})
Lookup(u) == /\ s.user[u] = "lookup"
    /\ s' = [s EXCEPT !.user[u] = IF Chip(u) \in s.registered THEN "acquire" ELSE "closed"]
Acquire(u) == /\ s.user[u] = "acquire"
    /\ s' = IF Chip(u) \in s.registered \/ Mutation = "unguarded-open"
        THEN [s EXCEPT !.user[u] = "open", !.badOpen = @ \/ s.pc # "live"]
        ELSE [s EXCEPT !.user[u] = "closed"]
StartIO(u) == /\ s.user[u] = "open"
    /\ s' = IF Policy = "gate-and-drain" /\ s.pc # "live"
        THEN [s EXCEPT !.user[u] = "done"]
        ELSE [s EXCEPT !.user[u] = "io", !.active = @ \cup {u},
              !.badIO = @ \/ Chip(u) \notin s.resources]
FinishIO(u) == /\ s.user[u] = "io"
    /\ s' = [s EXCEPT !.user[u] = "done", !.active = @ \ {u},
        !.badIO = @ \/ Chip(u) \notin s.resources]
Close(u) == /\ s.user[u] = "done"
    /\ s' = [s EXCEPT !.user[u] = "closed",
        !.registered = IF Policy = "source" /\ s.pc # "live" /\ Refs(Chip(u)) = 1
                       THEN @ \ {Chip(u)} ELSE @]
Unregister == /\ s.pc = "live"
    /\ s' = [s EXCEPT !.registered = IF Policy = "source"
        THEN {c \in s.registered : \E u \in Users : Chip(u) = c /\
            s.user[u] \in {"open", "io", "done"}} ELSE {},
        !.owners = {}, !.partitions = {},
        !.pc = "cleanup"]
Cleanup == /\ s.pc = "cleanup" /\ s.next \in Chips
    /\ (Policy # "defer-to-last-ref" \/ Refs(s.next) = 0)
    /\ (Policy # "gate-and-drain" \/ Mutation = "skip-drain" \/
        \A u \in s.active : Chip(u) # s.next)
    /\ s' = [s EXCEPT !.resources = @ \ {s.next}, !.next = @ + 1,
        !.badIO = @ \/ (\E u \in s.active : Chip(u) = s.next)]
Finish == /\ s.pc = "cleanup" /\ s.next = 3
    /\ \A u \in Users : s.user[u] = "closed"
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
UserStep == \E u \in Users : Lookup(u) \/ Acquire(u) \/ StartIO(u) \/ FinishIO(u) \/ Close(u)
Next == UserStep \/ Unregister \/ Cleanup \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next) /\ WF_vars(UserStep)
ResourcesForIO == ~s.badIO
NoNewOpen == ~s.badOpen
PartitionPinsMaster == \A c \in s.partitions : Refs(c) >= 2
AllReleased == s.pc = "done" => s.resources = {} /\ \A c \in Chips : Refs(c) = 0
Terminates == <> (s.pc = "done")
=============================================================================
