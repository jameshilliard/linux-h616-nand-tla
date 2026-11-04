---------------------------- MODULE NandProvider ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Non-cyclic DMA: command completion, provider IRQ, callback tasklet,
\* producer stop, and outstanding memory writes are different obligations.
\* sunxi NAND installs no client callback; CallbackUsesMapping is FALSE there.
EXTENDS Integers
CONSTANTS CommandOrdersMemory, TerminationOrdersMemory, Synchronize,
          CallbackUsesMapping, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "running", mapped |-> TRUE, pending |-> TRUE,
    producer |-> TRUE, command |-> FALSE, irq |-> FALSE, queued |-> FALSE,
    callback |-> FALSE, stopped |-> FALSE, unsafe |-> FALSE, delivered |-> 0]
BusDrain == /\ s.pending
    /\ s' = [s EXCEPT !.pending = FALSE]
Command == /\ s.pc = "running" /\ ~s.command
    /\ (~CommandOrdersMemory \/ ~s.pending)
    /\ s' = [s EXCEPT !.command = TRUE]
ProviderIRQ == /\ s.pc = "running" /\ ~s.irq /\ ~s.pending
    /\ s' = [s EXCEPT !.irq = TRUE, !.producer = FALSE, !.queued = TRUE]
CallbackStart == /\ s.queued /\ ~s.callback
    /\ s' = [s EXCEPT !.queued = FALSE, !.callback = TRUE]
CallbackFinish == /\ s.callback
    /\ s' = [s EXCEPT !.callback = FALSE, !.delivered = @ + 1,
        !.unsafe = @ \/ (CallbackUsesMapping /\ ~s.mapped)]
WaitDone == /\ s.pc = "running" /\ s.command
    /\ s' = [s EXCEPT !.pc = "cleanup", !.producer = FALSE]
Timeout == /\ s.pc = "running"
    /\ s' = [s EXCEPT !.pc = "stop"]
Stop == /\ s.pc = "stop"
    /\ (~TerminationOrdersMemory \/ ~s.pending)
    /\ s' = [s EXCEPT !.producer = FALSE, !.stopped = TRUE,
        !.queued = FALSE, !.pc = "cleanup"]
Unmap == /\ s.pc = "cleanup"
    /\ (~Synchronize \/ (~s.callback /\ ~s.queued))
    /\ s' = [s EXCEPT !.mapped = FALSE, !.pc = "done",
        !.unsafe = @ \/ s.pending \/ s.producer]
Done == s.pc = "done" /\ ~s.callback /\ ~s.queued /\ UNCHANGED vars
Next == BusDrain \/ Command \/ ProviderIRQ \/ CallbackStart \/ CallbackFinish \/ WaitDone \/ Timeout \/ Stop \/ Unmap \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next) /\ WF_vars(BusDrain) /\ WF_vars(CallbackFinish) /\ WF_vars(CallbackStart)
SafeUnmap == ~s.unsafe
OneCompletion == s.delivered <= 1
Terminates == <> (s.pc = "done" /\ ~s.callback /\ ~s.queued)
=============================================================================
