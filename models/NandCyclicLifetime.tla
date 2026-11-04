----------------------- MODULE NandCyclicLifetime -----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* One sun4i vchan: IRQ association lookup, locked callback enqueue,
\* tasklet capture/execution, atomic termination, and sleepable draining.
\* No new issue_pending between termination and synchronization (API contract).
\* Physical-channel reuse by another vchan and bus retirement are not modeled.
EXTENDS Integers
CONSTANTS RetainUntilSync, Mutation
VARIABLE s
vars == <<s>>
Init == s = [irq |-> "lookup", task |-> "idle", cpu |-> "terminate",
    owner |-> "none", active |-> TRUE, cached |-> FALSE,
    queued |-> FALSE, freed |-> FALSE, unsafe |-> FALSE]
IrqLookup ==
    /\ s.irq = "lookup"
    /\ s' = [s EXCEPT !.cached = s.active,
        !.irq = IF s.active THEN "lock" ELSE "done"]
IrqLock ==
    /\ s.irq = "lock" /\ s.owner = "none"
    /\ s' = [s EXCEPT !.owner = "irq", !.irq = "enqueue"]
IrqEnqueue ==
    /\ s.irq = "enqueue" /\ s.owner = "irq"
    /\ LET use == IF RetainUntilSync THEN s.active ELSE s.cached
       IN s' = [s EXCEPT !.queued = @ \/ use,
           !.unsafe = @ \/ (use /\ s.freed), !.irq = "unlock"]
IrqUnlock ==
    /\ s.irq = "unlock" /\ s.owner = "irq"
    /\ s' = [s EXCEPT !.owner = "none", !.irq = "done"]
TaskCapture ==
    /\ s.task = "idle" /\ s.queued /\ s.owner = "none"
    /\ s' = [s EXCEPT !.task = "running", !.queued = FALSE,
        !.unsafe = @ \/ s.freed]
TaskFinish ==
    /\ s.task = "running"
    /\ s' = [s EXCEPT !.task = "idle", !.unsafe = @ \/ s.freed]
Terminate ==
    /\ s.cpu = "terminate" /\ s.owner = "none"
    /\ s' = [s EXCEPT !.active = FALSE, !.cpu = "sync-irq",
        !.queued = IF RetainUntilSync THEN FALSE ELSE @,
        !.freed = ~RetainUntilSync]
DrainIRQ ==
    /\ s.cpu = "sync-irq" /\ s.irq = "done"
    /\ s' = [s EXCEPT !.cpu = "sync-task"]
DrainTask ==
    /\ s.cpu = "sync-task" /\ s.task = "idle" /\ ~s.queued
    /\ s' = [s EXCEPT !.freed = TRUE, !.cpu = "done"]
Done == s.cpu = "done" /\ UNCHANGED vars
IRQ == IrqLookup \/ IrqLock \/ IrqEnqueue \/ IrqUnlock
Task == TaskCapture \/ TaskFinish
CPU == Terminate \/ DrainIRQ \/ DrainTask
Next == IRQ \/ Task \/ CPU \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(IRQ) /\ WF_vars(Task) /\ WF_vars(CPU)
NoUseAfterFree == ~s.unsafe /\ (s.task = "running" => ~s.freed)
DrainedAtReturn == s.cpu = "done" =>
    s.irq = "done" /\ s.task = "idle" /\ ~s.queued /\ s.freed
Terminates == <> (s.cpu = "done")
=============================================================================
