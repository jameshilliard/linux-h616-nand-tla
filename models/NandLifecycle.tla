---------------------------- MODULE NandLifecycle ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Ordered probe ownership, two registered chips, serialized chip selection,
\* and removal. Core drain and successful clock programming are assumptions.
EXTENDS Integers, FiniteSets, Sequences
CONSTANTS FailAt, CoreDrains, ClockWorks, Mutation
VARIABLE s
vars == <<s>>
Resources == <<"memory","mmio","clocks","reset","irq","dma","chip1","chip2">>
Init == s = [pc |-> "probe", n |-> 1, held |-> {}, published |-> {},
    accepting |-> FALSE, active |-> 0, selected |-> 0, clock |-> 0, geometry |-> 0,
    operations |-> 0, unsafe |-> FALSE, wrong |-> FALSE, irqRunning |-> FALSE]
Acquire ==
    /\ s.pc = "probe" /\ s.n <= Len(Resources)
    /\ s' = IF s.n = FailAt THEN [s EXCEPT !.pc = "unregister"] ELSE
        [s EXCEPT !.held = @ \cup {Resources[s.n]}, !.n = @ + 1,
            !.published = IF s.n >= 7 THEN @ \cup {s.n - 6} ELSE @]
Publish ==
    /\ s.pc = "probe" /\ s.n > Len(Resources)
    /\ s' = [s EXCEPT !.pc = "live", !.accepting = TRUE]
Begin ==
    /\ s.pc = "live" /\ s.accepting /\ s.active = 0 /\ s.operations < 2
    /\ LET chip == s.operations + 1 IN
        s' = [s EXCEPT !.active = chip, !.selected = chip,
            !.clock = IF ClockWorks THEN chip ELSE @,
            !.geometry = IF Mutation = "stale-geometry" /\ chip = 2 THEN @ ELSE chip,
            !.operations = @ + 1]
Transfer ==
    /\ s.active # 0
    /\ s' = [s EXCEPT !.wrong = @ \/ s.selected # s.active \/
                         s.geometry # s.active \/ s.clock # s.active,
        !.unsafe = @ \/ ~("dma" \in s.held) \/ ~(s.active \in s.published),
        !.active = 0]
IrqStart ==
    /\ s.active # 0 /\ "irq" \in s.held /\ ~s.irqRunning
    /\ s' = [s EXCEPT !.irqRunning = TRUE]
IrqFinish ==
    /\ s.irqRunning
    /\ s' = [s EXCEPT !.irqRunning = FALSE,
        !.unsafe = @ \/ ~("memory" \in s.held) \/ ~("mmio" \in s.held)]
Remove ==
    /\ s.pc = "live" /\ s.operations = 2
    /\ s' = [s EXCEPT !.accepting = FALSE, !.pc = "unregister"]
Unregister ==
    /\ s.pc = "unregister"
    /\ ~CoreDrains \/ s.active = 0
    /\ s' = [s EXCEPT !.published = {}, !.held = @ \ {"chip1","chip2"},
        !.unsafe = @ \/ s.active # 0, !.pc = "release"]
Release ==
    /\ s.pc = "release" /\ s.held # {}
    /\ LET candidates == {i \in 1..Len(Resources) : Resources[i] \in s.held}
           last == CHOOSE i \in candidates : \A j \in candidates : j <= i
           resource == IF Mutation = "free-before-irq" /\ "memory" \in s.held
                       THEN "memory" ELSE Resources[last]
       IN /\ resource # "irq" \/ ~s.irqRunning
          /\ s' = [s EXCEPT !.held = @ \ {resource},
              !.unsafe = @ \/ (resource = "memory" /\ "irq" \in s.held)]
Finish ==
    /\ s.pc = "release" /\ s.held = {}
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Cpu == Acquire \/ Publish \/ Begin \/ Transfer \/ Remove \/ Unregister \/ Release \/ Finish
Next == Cpu \/ IrqStart \/ IrqFinish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Cpu) /\ WF_vars(IrqFinish)
NoUseAfterRelease == ~s.unsafe
CorrectChipContext == ~s.wrong
PublishedOwned == \A chip \in s.published :
    (IF chip = 1 THEN "chip1" ELSE "chip2") \in s.held
NoLeak == s.pc = "done" => s.held = {} /\ s.published = {} /\ ~s.irqRunning
Terminates == <> (s.pc = "done")
=============================================================================
