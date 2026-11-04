-------------------------------- MODULE NandRt --------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Scheduling contract around disable_irq/synchronize_irq on PREEMPT_RT.
\* Safety does not require bounded scheduling latency; termination does require
\* runnable IRQ service. "ticks" saturates and is not a claimed time bound.
EXTENDS Integers
CONSTANTS FairIrq, Mutation
VARIABLE s
vars == <<s>>
Init == s = [cpu |-> "wait", irq |-> "status", disabled |-> FALSE,
    status |-> TRUE, mask |-> TRUE, snapshot |-> FALSE, hi |-> FALSE,
    completed |-> FALSE, ticks |-> 0, reused |-> FALSE, stale |-> FALSE]
ReadStatus ==
    /\ s.irq = "status"
    /\ s' = [s EXCEPT !.snapshot = s.status, !.irq = "mask"]
ReadMask ==
    /\ s.irq = "mask"
    /\ s' = [s EXCEPT !.hi = s.mask, !.irq = "ack"]
Ack ==
    /\ s.irq = "ack"
    /\ s' = [s EXCEPT !.status = FALSE, !.irq = "disable", !.stale = s.reused]
Mask ==
    /\ s.irq = "disable"
    /\ s' = [s EXCEPT !.mask = FALSE, !.irq = "complete", !.stale = @ \/ s.reused]
Complete ==
    /\ s.irq = "complete"
    /\ s' = [s EXCEPT !.completed = s.snapshot /\ s.hi, !.irq = "idle",
                      !.stale = @ \/ s.reused]
Timeout ==
    /\ s.cpu = "wait"
    /\ s' = [s EXCEPT !.disabled = TRUE, !.cpu = "drain"]
Drain ==
    /\ s.cpu = "drain" /\ (s.irq = "idle" \/ Mutation = "skip-synchronize")
    /\ s' = [s EXCEPT !.cpu = "check"]
Check ==
    /\ s.cpu = "check"
    /\ s' = [s EXCEPT !.cpu = "reuse"]
Reuse ==
    /\ s.cpu = "reuse"
    /\ s' = [s EXCEPT !.reused = TRUE, !.disabled = FALSE, !.cpu = "done"]
Tick ==
    /\ s.cpu # "done"
    /\ s' = [s EXCEPT !.ticks = IF @ < 3 THEN @ + 1 ELSE @]
Done == s.cpu = "done" /\ s.irq = "idle" /\ UNCHANGED vars
Cpu == Timeout \/ Drain \/ Check \/ Reuse
Irq == ReadStatus \/ ReadMask \/ Ack \/ Mask \/ Complete
Next == Cpu \/ Irq \/ Tick \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Cpu) /\
    (IF FairIrq THEN WF_vars(Irq) ELSE TRUE)
NoStaleAccess == ~s.stale
DrainBeforeReuse == s.reused => s.irq = "idle"
Terminates == <> (s.cpu = "done" /\ s.irq = "idle")
=============================================================================
