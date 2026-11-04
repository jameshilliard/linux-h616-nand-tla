------------------------ MODULE NandRequestOrdering ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Two source-shaped request lifetimes sharing a controller. IRQ accesses are
\* separate actions, with timeout/drain/abort/unmap between requests. Physical
\* retirement and old-event cancellation are explicit provider/RTL contracts.
EXTENDS Integers, FiniteSets
CONSTANTS Internal, StopOK, RetireBus, RetireEvents, Mutation
VARIABLE s
vars == <<s>>
Needed == IF Internal THEN {"cmd", "dma"} ELSE {"cmd"}
Init == s = [op |-> 0, cpu |-> "next", irq |-> "idle", owner |-> 0,
    mapped |-> FALSE, active |-> FALSE, bus |-> FALSE, future |-> {},
    status |-> {}, mask |-> {}, arrived |-> {}, complete |-> FALSE,
    disabled |-> FALSE, hs |-> {}, hi |-> {}, irqOp |-> 0,
    failed |-> FALSE, timeout |-> FALSE, unsafe |-> FALSE, stale |-> FALSE,
    badWake |-> FALSE, ackOld |-> FALSE, oldAck |-> FALSE, result |-> "unset"]
Launch == /\ s.cpu = "next" /\ s.op < 2
    /\ s' = [s EXCEPT !.op = @ + 1, !.owner = s.op + 1, !.cpu = "arm",
        !.mapped = TRUE, !.active = TRUE, !.bus = TRUE,
        !.future = @ \cup ({s.op+1} \X Needed), !.arrived = {},
        !.complete = FALSE, !.result = "unset", !.timeout = FALSE]
Arm == /\ s.cpu = "arm"
    /\ s' = [s EXCEPT !.mask = Needed, !.cpu = "wait"]
Arrive == /\ s.future # {}
    /\ \E event \in s.future:
        LET mine == event[1] = s.op
            arrived == IF mine THEN s.arrived \cup {event[2]} ELSE s.arrived
            finished == mine /\ arrived = Needed
        IN s' = [s EXCEPT !.future = @ \ {event}, !.status = @ \cup {event},
            !.arrived = arrived, !.active = IF finished THEN FALSE ELSE @,
            !.bus = IF finished /\ RetireBus THEN FALSE ELSE @]
\* The last bus transaction may retire before the completion indication.
Retire == /\ s.bus
    /\ s' = [s EXCEPT !.bus = FALSE, !.unsafe = @ \/ ~s.mapped]
IrqStatus == /\ s.irq = "idle" /\ ~s.disabled
    /\ {e[2] : e \in s.status} \cap s.mask # {}
    /\ s' = [s EXCEPT !.hs = s.status, !.irqOp = s.op, !.irq = "mask-read"]
IrqMaskRead == /\ s.irq = "mask-read"
    /\ s' = [s EXCEPT !.hi = s.mask, !.irq = "ack"]
IrqAck == /\ s.irq = "ack"
    /\ s' = [s EXCEPT !.status = {e \in @ : e[2] \notin {x[2] : x \in s.hs}},
        !.oldAck = @ \/ (\E e \in s.hs : e[1] # s.op /\ e[2] \in s.hi),
        !.stale = @ \/ s.irqOp # s.op, !.irq = "mask-write"]
IrqMaskWrite == /\ s.irq = "mask-write"
    /\ s' = [s EXCEPT !.mask = s.hi \ {e[2] : e \in s.hs},
        !.stale = @ \/ s.irqOp # s.op, !.irq = "signal"]
IrqSignal == /\ s.irq = "signal"
    /\ s' = [s EXCEPT !.complete = @ \/ (s.hi # {} /\ s.hi \subseteq {e[2] : e \in s.hs}),
        !.irq = "idle"]
Wake == /\ s.cpu = "wait" /\ s.complete
    /\ s' = [s EXCEPT !.cpu = "clear", !.result = "ok",
        !.badWake = @ \/ s.arrived # Needed]
Timeout == /\ s.cpu = "wait"
    /\ s' = [s EXCEPT !.cpu = "drain", !.disabled = TRUE]
Drain == /\ s.cpu = "drain" /\ (s.irq = "idle" \/ Mutation = "no-drain")
    /\ s' = [s EXCEPT !.cpu = "recheck"]
Recheck == /\ s.cpu = "recheck"
    /\ LET ok == s.mask \subseteq {e[2] : e \in s.status}
       IN s' = [s EXCEPT !.result = IF ok THEN "ok" ELSE "timeout",
           !.timeout = ~ok, !.badWake = @ \/ (ok /\ s.arrived # Needed),
           !.cpu = "clear"]
Clear == /\ s.cpu = "clear"
    /\ s' = [s EXCEPT !.mask = {}, !.status = {}, !.disabled = FALSE,
        !.cpu = IF s.timeout THEN "abort" ELSE "unmap"]
Abort == /\ s.cpu = "abort"
    /\ s' = [s EXCEPT !.active = IF StopOK THEN FALSE ELSE @,
        !.bus = IF StopOK /\ RetireBus THEN FALSE ELSE @,
        !.future = IF StopOK /\ RetireEvents THEN {} ELSE @,
        !.status = IF StopOK /\ RetireEvents THEN {} ELSE @,
        !.cpu = "unmap", !.failed = ~StopOK]
Unmap == /\ s.cpu = "unmap"
    /\ s' = [s EXCEPT !.unsafe = @ \/ s.active \/ s.bus,
        !.mapped = FALSE, !.owner = 0, !.cpu = IF s.op = 2 THEN "done" ELSE "next"]
Done == s.cpu = "done" /\ s.irq = "idle" /\ s.future = {} /\ ~s.bus /\ UNCHANGED vars
Cpu == Launch \/ Arm \/ Wake \/ Timeout \/ Drain \/ Recheck \/ Clear \/ Abort \/ Unmap
Irq == IrqStatus \/ IrqMaskRead \/ IrqAck \/ IrqMaskWrite \/ IrqSignal
Next == Cpu \/ Irq \/ Arrive \/ Retire \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Cpu) /\ WF_vars(Irq) /\ WF_vars(Arrive) /\ WF_vars(Retire)
Owner == s.mapped => s.owner = s.op
MappingSafe == ~s.unsafe
NoStaleHandler == ~s.stale
FreshCompletion == ~s.badWake
ReturnedClean == s.cpu = "next" => ~s.mapped /\ s.mask = {} /\ ~s.disabled
Terminates == <> (s.cpu = "done" /\ s.irq = "idle" /\ s.future = {} /\ ~s.bus)
=============================================================================
