-------------------------- MODULE NandRecovery --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Timeout backend contracts, deliberately separating command, DMA producer,
\* outstanding memory transaction, interrupt handler and latched event state.
\* A software IRQ drain is not a bus/DMA fence. No NAND client DMA callback
\* is installed by this driver; provider-internal lifetime is out of scope.
EXTENDS Integers
CONSTANTS Backend, StopSucceeds, BusDrains, CommandStops, EventsStop, Mutation
VARIABLE s
vars == <<s>>
Dma == Backend # "pio"
Internal == Backend \in {"register", "descriptor"}
Init == s = [pc |-> "timeout", irq |-> "idle", irqEnabled |-> TRUE,
    event |-> FALSE, futureEvent |-> TRUE, command |-> TRUE, producer |-> Dma,
    bus |-> Dma, mapped |-> Dma, epoch |-> 1, latchedEpoch |-> 0,
    irqEpoch |-> 0, unsafe |-> FALSE, stale |-> FALSE, reusedBusy |-> FALSE,
    resetIssued |-> FALSE, providerStopped |-> FALSE]
Event ==
    /\ s.futureEvent
    /\ s' = [s EXCEPT !.event = TRUE, !.latchedEpoch = 1, !.futureEvent = FALSE]
IrqBegin ==
    /\ s.irqEnabled /\ s.event /\ s.irq = "idle"
    /\ s' = [s EXCEPT !.irq = "finish", !.irqEpoch = s.latchedEpoch, !.event = FALSE]
IrqFinish ==
    /\ s.irq = "finish"
    /\ s' = [s EXCEPT !.stale = @ \/ s.irqEpoch # s.epoch, !.irq = "idle"]
Memory ==
    /\ s.bus
    /\ s' = [s EXCEPT !.unsafe = @ \/ ~s.mapped, !.bus = FALSE]
Timeout ==
    /\ s.pc = "timeout"
    /\ s' = [s EXCEPT !.irqEnabled = FALSE, !.pc = "drain"]
Drain ==
    /\ s.pc = "drain" /\ (s.irq = "idle" \/ Mutation = "skip-irq-drain")
    /\ s' = [s EXCEPT !.pc = "clear"]
Clear ==
    /\ s.pc = "clear"
    /\ s' = [s EXCEPT !.event = FALSE, !.pc = "abort"]
Abort ==
    /\ s.pc = "abort"
    /\ s' = [s EXCEPT !.resetIssued = Internal,
        !.providerStopped = Backend \in {"sun4i", "sun6i"},
        !.producer = IF StopSucceeds THEN FALSE ELSE @,
        !.bus = IF StopSucceeds /\ BusDrains THEN FALSE ELSE @,
        !.command = IF CommandStops THEN FALSE ELSE @,
        !.event = IF EventsStop THEN FALSE ELSE @,
        !.futureEvent = IF EventsStop THEN FALSE ELSE @,
        !.pc = "cleanup"]
Cleanup ==
    /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.unsafe = @ \/ (s.mapped /\ (s.producer \/ s.bus)),
        !.mapped = FALSE, !.pc = "reuse"]
Reuse ==
    /\ s.pc = "reuse"
    /\ s' = [s EXCEPT !.epoch = 2, !.irqEnabled = TRUE,
        !.reusedBusy = s.command, !.pc = "settle"]
Settle ==
    /\ s.pc = "settle" /\ ~s.futureEvent /\ ~s.event /\ s.irq = "idle" /\ ~s.bus
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Cpu == Timeout \/ Drain \/ Clear \/ Abort \/ Cleanup \/ Reuse \/ Settle
Irq == IrqBegin \/ IrqFinish
Next == Cpu \/ Event \/ Irq \/ Memory \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Cpu) /\ WF_vars(Event) /\ WF_vars(Irq) /\ WF_vars(Memory)
SafeMemory == ~s.unsafe
SafeCommandReuse == ~s.reusedBusy
NoOldCompletion == ~s.stale
BackendSelection == s.pc \in {"cleanup", "reuse", "settle", "done"} =>
    s.resetIssued = Internal /\ s.providerStopped = (Backend \in {"sun4i", "sun6i"})
Terminates == <> (s.pc = "done")
=============================================================================
