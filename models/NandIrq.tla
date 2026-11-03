------------------------------ MODULE NandIrq ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, FiniteSets
CONSTANTS Events, Operations, Mutation, Polling
VARIABLE s
vars == <<s>>
Init == s =
    [epoch |-> 0, cpu |-> "next", irq |-> "idle",
     hs |-> {}, hi |-> {}, hgen |-> 0, mask |-> {}, status |-> {},
     arrived |-> {}, completion |-> FALSE, cgen |-> 0,
     disabled |-> FALSE, result |-> "unset", atCheck |-> FALSE,
     stale |-> FALSE, wrongCompletion |-> FALSE]
Begin ==
    /\ s.cpu = "next" /\ s.epoch < Operations
    /\ s' = [s EXCEPT !.epoch = @ + 1, !.cpu = "arm",
                     !.arrived = {}, !.result = "unset", !.atCheck = FALSE,
                     !.completion = FALSE]
Arm ==
    /\ s.cpu = "arm"
    /\ s' = [s EXCEPT !.mask = IF Polling THEN {} ELSE Events, !.cpu = "wait"]
Arrive ==
    /\ s.cpu \in {"arm", "wait", "drain", "check"}
    /\ \E e \in Events \ s.arrived :
         s' = [s EXCEPT !.status = @ \cup {e}, !.arrived = @ \cup {e}]
IrqReadStatus ==
    /\ s.irq = "idle" /\ ~s.disabled /\ s.mask \cap s.status # {}
    /\ s' = [s EXCEPT !.irq = "read-mask", !.hs = s.status, !.hgen = s.epoch]
IrqReadMask ==
    /\ s.irq = "read-mask"
    /\ s' = [s EXCEPT !.hi = s.mask,
         !.irq = IF s.hs \cap s.mask = {} THEN "idle"
                 ELSE IF Mutation = "complete-before-registers"
                      THEN "signal" ELSE "ack"]
IrqAck ==
    /\ s.irq = "ack"
    /\ s' = [s EXCEPT !.status = @ \ s.hs, !.irq = "mask",
                     !.stale = @ \/ s.hgen # s.epoch]
IrqMask ==
    /\ s.irq = "mask"
    /\ s' = [s EXCEPT !.mask = s.hi \ s.hs,
         !.stale = @ \/ s.hgen # s.epoch,
         !.irq = IF Mutation = "complete-before-registers" THEN "idle"
                 ELSE "signal"]
IrqSignal ==
    /\ s.irq = "signal"
    /\ s' = [s EXCEPT
         !.completion = @ \/ (s.hi # {} /\ s.hi \subseteq s.hs),
         !.cgen = IF s.hi # {} /\ s.hi \subseteq s.hs THEN s.hgen ELSE @,
         !.irq = IF Mutation = "complete-before-registers" THEN "ack" ELSE "idle"]
Wake ==
    /\ s.cpu = "wait" /\ s.completion /\ ~Polling
    /\ s' = [s EXCEPT !.cpu = "cleanup", !.result = "ok",
                     !.wrongCompletion = @ \/ s.cgen # s.epoch]
Timeout ==
    /\ s.cpu = "wait"
    /\ s' = [s EXCEPT !.cpu = IF Polling THEN "check" ELSE "drain",
                     !.disabled = ~Polling]
PollReady ==
    /\ s.cpu = "wait" /\ Polling /\ Events \subseteq s.status
    /\ s' = [s EXCEPT !.cpu = "cleanup", !.result = "ok"]
Drain ==
    /\ s.cpu = "drain"
    /\ s.irq = "idle" \/ Mutation = "no-drain"
    /\ s' = [s EXCEPT !.cpu = "check"]
Recheck ==
    /\ s.cpu = "check"
    /\ LET pending == IF Polling \/ Mutation = "recheck-original-mask"
                      THEN Events ELSE s.mask \cap Events
       IN s' = [s EXCEPT !.cpu = "cleanup",
                         !.result = IF pending \subseteq s.status THEN "ok" ELSE "timeout",
                         !.atCheck = Events \subseteq s.arrived]
Cleanup ==
    /\ s.cpu = "cleanup"
    /\ s' = [s EXCEPT !.mask = {}, !.cpu = "clear"]
Clear ==
    /\ s.cpu = "clear"
    /\ s' = [s EXCEPT !.status = @ \ Events, !.cpu = "enable"]
Enable ==
    /\ s.cpu = "enable"
    /\ s' = [s EXCEPT !.disabled = FALSE, !.cpu = "returned"]
Return ==
    /\ s.cpu = "returned"
    /\ s' = [s EXCEPT !.cpu = IF s.epoch = Operations THEN "done" ELSE "next"]
Done == s.cpu = "done" /\ s.irq = "idle" /\ UNCHANGED vars
Cpu == Begin \/ Arm \/ Wake \/ Timeout \/ PollReady \/ Drain \/ Recheck \/ Cleanup \/
       Clear \/ Enable \/ Return
Irq == IrqReadStatus \/ IrqReadMask \/ IrqAck \/ IrqMask \/ IrqSignal
Next == Cpu \/ Irq \/ Arrive \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Cpu) /\ WF_vars(Irq)
NoStaleRegisterWrites == ~s.stale
NoStaleCompletion == ~s.wrongCompletion
SuccessHasEvents == s.result = "ok" => Events \subseteq s.arrived
DrainedEventsNotLost ==
    s.atCheck /\ s.result # "unset" => s.result = "ok"
ReturnedClean == s.cpu \in {"returned", "next", "done"} =>
    s.mask = {} /\ s.status = {} /\ ~s.disabled
Terminates == <> (s.cpu = "done" /\ s.irq = "idle")
=============================================================================
