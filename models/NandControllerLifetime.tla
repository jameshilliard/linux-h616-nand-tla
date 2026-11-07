---------------------- MODULE NandControllerLifetime ----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Two bindings share physical resources over time; each binding owns multiple
\* chips. Software storage survives removal while handles/device refs remain.
\* Operations abstract the NAND chip/controller locks. Timing, DMA and register
\* semantics are outside this model and covered by the controller models.
EXTENDS Integers, FiniteSets
CONSTANTS Chips, RetainController, RetainDevices, Drain, ReuseSlot
VARIABLE s
vars == <<s>>
Generations == 1..2
Objects == Generations \X (1..Chips)
Of(g) == {c \in Objects : c[1]=g}
Init == s=[pc |-> [c \in Objects |-> IF c[1]=1 THEN "read" ELSE "absent"],
    published |-> Of(1), disconnected |-> {}, driver |-> Of(1),
    handles |-> Of(1), devices |-> Of(1), metadata |-> Of(1),
    active |-> {}, binding |-> {1}, hardware |-> {1}, controller |-> {1},
    target |-> [c \in Objects |-> c[1]], replacement |-> FALSE,
    badStorage |-> FALSE, badHardware |-> FALSE, retargeted |-> FALSE]

Read(c) == /\ s.pc[c]="read"
    /\ (~(c \in s.disconnected) =>
           ~\E a \in s.active : s.target[a]=s.target[c])
    /\ LET go == c \notin s.disconnected
       IN s'=[s EXCEPT !.pc[c]=IF go THEN "active" ELSE "put",
            !.active=IF go THEN @ \cup {c} ELSE @,
            !.badStorage=@ \/ c \notin s.metadata \/ c[1] \notin s.controller,
            !.badHardware=@ \/ (go /\ s.target[c] \notin s.hardware),
            !.retargeted=@ \/ (go /\ s.target[c] # c[1])]
Complete(c) == /\ s.pc[c]="active"
    /\ s'=[s EXCEPT !.pc[c]="put",!.active=@ \ {c},
         !.badStorage=@ \/ c \notin s.metadata \/ c[1] \notin s.controller,
         !.badHardware=@ \/ s.target[c] \notin s.hardware]
Put(c) == /\ s.pc[c]="put"
    /\ s'=[s EXCEPT !.pc[c]="done",!.handles=@ \ {c},
         !.badStorage=@ \/ c \notin s.metadata]
Unpublish(c) == /\ c \in s.published
    /\ s'=[s EXCEPT !.published=@ \ {c}]
Disconnect(c) == /\ c \in s.driver /\ c \notin s.published \cup s.disconnected
    /\ (~Drain \/ c \notin s.active)
    /\ s'=[s EXCEPT !.disconnected=@ \cup {c}]
DropChip(c) == /\ c \in s.driver /\ c \in s.disconnected
    /\ s'=[s EXCEPT !.driver=@ \ {c}]
DevicePut(c) == /\ c \in s.devices /\ c \notin s.published
    /\ s'=[s EXCEPT !.devices=@ \ {c},!.badStorage=@ \/ c \notin s.metadata]
ChipRelease(c) == /\ c \in s.metadata /\ c \notin s.driver \cup s.handles
    /\ (~RetainDevices \/ c \notin s.devices)
    /\ s'=[s EXCEPT !.metadata=@ \ {c},
         !.badStorage=@ \/ c[1] \notin s.controller]
HardwareOff(g) == /\ g \in s.hardware
    /\ Of(g) \cap s.driver={} /\ Of(g) \subseteq s.disconnected
    /\ s'=[s EXCEPT !.hardware=@ \ {g},
         !.badHardware=@ \/ (Of(g) \cap s.active # {})]
DropBinding(g) == /\ g \in s.binding /\ g \notin s.hardware
    /\ s'=[s EXCEPT !.binding=@ \ {g}]
ControllerRelease(g) == /\ g \in s.controller /\ g \notin s.binding
    /\ (~RetainController \/ Of(g) \cap s.metadata={})
    /\ s'=[s EXCEPT !.controller=@ \ {g}]
RegisterReplacement == /\ ~s.replacement /\ 1 \notin s.binding
    /\ s'=[s EXCEPT !.replacement=TRUE,
         !.pc=[c \in Objects |-> IF c[1]=2 THEN "read" ELSE s.pc[c]],
         !.published=@ \cup Of(2),!.driver=@ \cup Of(2),
         !.handles=@ \cup Of(2),!.devices=@ \cup Of(2),!.metadata=@ \cup Of(2),
         !.binding=@ \cup {2},!.hardware=@ \cup {2},!.controller=@ \cup {2},
         !.disconnected=IF ReuseSlot THEN {} ELSE @,
         !.target=IF ReuseSlot THEN [c \in Objects |-> 2] ELSE @]

Finish == s.replacement /\ s.metadata={} /\ s.controller={} /\
          s.devices={} /\ \A c \in Objects:s.pc[c]="done"
Done == Finish /\ UNCHANGED vars
Next == (\E c \in Objects: Read(c) \/ Complete(c) \/ Put(c) \/ Unpublish(c) \/
             Disconnect(c) \/ DropChip(c) \/ DevicePut(c) \/ ChipRelease(c)) \/
        (\E g \in Generations: HardwareOff(g) \/ DropBinding(g) \/ ControllerRelease(g)) \/
        RegisterReplacement \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
MetadataStorage == ~s.badStorage /\ s.handles \cup s.devices \subseteq s.metadata
ControllerStorage == \A c \in s.metadata: c[1] \in s.controller
HardwareDrained == ~s.badHardware
HandleIsolation == ~s.retargeted
Exclusive == \A g \in Generations: Cardinality({c \in s.active:s.target[c]=g})<=1
Terminates == <>Finish
=============================================================================
