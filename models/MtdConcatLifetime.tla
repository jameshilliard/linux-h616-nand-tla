------------------------- MODULE MtdConcatLifetime -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Two component slots, with one replacement of slot 1. A publication owns
\* an immutable snapshot independent of the staging list. File/NVMEM handles
\* may retain that publication after either physical master is removed.
\* Backing pins are independent of whether the master's device was published.
\* IRQ/DMA quiescence is covered by NandRetainedLifetime, not this model.
EXTENDS Integers, FiniteSets, Sequences
CONSTANTS RetainBacking, RetainComponents, RequireReady, ClearFailed
Ids == 1..3
Slots == 1..2
Slot(i) == IF i = 3 THEN 1 ELSE i
VARIABLE s
vars == <<s>>
Init == s = [created |-> {}, owner |-> {}, alive |-> {}, parts |-> {},
             staged |-> [p \in Slots |-> 0], ready |-> {},
             snapshots |-> <<>>, published |-> {}, devices |-> {},
             files |-> {}, nvmem |-> {}, acquired |-> {},
             premature |-> FALSE]
Members(n) == {s.snapshots[n][p] : p \in Slots}
Staged == {s.staged[p] : p \in Slots} \ {0}
Pinned == UNION {Members(n) : n \in s.devices}

Probe(i) == /\ i \notin s.created /\ s.staged[Slot(i)] = 0
    /\ (i # 3 \/ (1 \in s.created /\ 1 \notin s.owner))
    /\ s' = [s EXCEPT !.created = @ \cup {i}, !.owner = @ \cup {i},
                      !.alive = @ \cup {i}, !.parts = @ \cup {i},
                      !.staged[Slot(i)] = i]
Ready(i) == /\ i \in Staged \cap s.owner /\ i \notin s.ready
    /\ s' = [s EXCEPT !.ready = @ \cup {i}]
Publish == /\ s.published = {} /\ Len(s.snapshots) < 2
    /\ \A p \in Slots: s.staged[p] # 0
    /\ (~RequireReady \/ Staged \subseteq s.ready)
    /\ LET n == Len(s.snapshots) + 1 IN
       s' = [s EXCEPT !.snapshots = Append(@, s.staged),
                      !.published = {n}, !.devices = @ \cup {n},
                      !.premature = ~ (Staged \subseteq s.ready)]
Acquire(n) == /\ n \in s.published /\ n \notin s.acquired
    /\ s' = [s EXCEPT !.acquired = @ \cup {n},
                      !.files = @ \cup {n}, !.nvmem = @ \cup {n}]
Unbind(i) == /\ i \in s.owner
    /\ s' = [s EXCEPT !.owner = @ \ {i}, !.ready = @ \ {i},
                      !.staged[Slot(i)] = 0,
                      !.published = {n \in @: i \notin Members(n)}]
FailProbe(i) == /\ i \in s.owner /\ i \notin s.ready
    /\ s' = [s EXCEPT !.owner = @ \ {i},
                      !.staged[Slot(i)] = IF ClearFailed THEN 0 ELSE @,
                      !.published = {n \in @: i \notin Members(n)}]
FilePut(n) == /\ n \in s.files
    /\ s' = [s EXCEPT !.files = @ \ {n}]
NvmemPut(n) == /\ n \in s.nvmem
    /\ s' = [s EXCEPT !.nvmem = @ \ {n}]
DeviceRelease(n) == /\ n \in s.devices
    /\ n \notin (s.published \cup s.files \cup s.nvmem)
    /\ s' = [s EXCEPT !.devices = @ \ {n}]
PartRelease(i) == /\ i \in s.parts /\ i \notin Staged
    /\ (~RetainComponents \/ i \notin Pinned)
    /\ s' = [s EXCEPT !.parts = @ \ {i}]
BackingRelease(i) == /\ i \in s.alive /\ i \notin s.owner
    /\ (~RetainBacking \/ i \notin s.parts)
    /\ s' = [s EXCEPT !.alive = @ \ {i}]
Finish == s.created = Ids /\ s.alive = {} /\ s.parts = {}
Done == Finish /\ UNCHANGED vars
Next == (\E i \in Ids: Probe(i) \/ Ready(i) \/ Unbind(i) \/ FailProbe(i) \/
                       PartRelease(i) \/ BackingRelease(i)) \/ Publish \/
        (\E n \in 1..2: Acquire(n) \/ FilePut(n) \/
                        NvmemPut(n) \/ DeviceRelease(n)) \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
BackingStorage == s.parts \subseteq s.alive
ComponentStorage == Pinned \subseteq s.parts
RegistrationComplete == ~s.premature
FailedProbeUnstaged == Staged \subseteq s.owner
SnapshotOrder == \A n \in s.devices: \A p \in Slots: Slot(s.snapshots[n][p]) = p
Terminates == <>Finish
=============================================================================
