----------------------- MODULE NandRegisterProtocol -----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* H6/H616 NDFC_ST/INT/CMD: one FIFO word, an independent command FSM,
\* sticky W1C events, and CMD/DMA auto-clear at FSM START, not FIFO write.
\* This is the register contract, not a replacement for NandIrq's split
\* handler accesses or NandRecovery's bus-retirement/timeout assumptions.
EXTENDS Integers, FiniteSets
CONSTANTS Operations, DMA, WaitRB, Polling, Order, Mutation
VARIABLE s
vars == <<s>>
Required == IF DMA THEN {"cmd", "dma"} ELSE {"cmd"}
Events == {"rb", "cmd", "dma"}
Init == s = [pc |-> "clear", op |-> 1, fifo |-> 0, fsm |-> 0,
    started |-> {}, cmd |-> {}, dma |-> {}, ready |-> {},
    status |-> Events, owner |-> [e \in Events |-> 0],
    remaining |-> Required, masked |-> TRUE,
    stale |-> FALSE, early |-> FALSE, accepted |-> {}]
Clear ==
    /\ s.pc = "clear"
    /\ s' = [s EXCEPT !.status = IF Mutation = "only-autoclear" THEN @
                                   ELSE @ \ Required,
                     !.remaining = Required, !.pc = "enqueue"]
Enqueue ==
    /\ s.pc = "enqueue" /\ s.fifo = 0
    /\ s' = [s EXCEPT !.fifo = s.op, !.pc = "arm"]
Latch ==
    /\ s.fifo # 0 /\ s.fsm = 0
    /\ s' = [s EXCEPT !.fsm = s.fifo, !.fifo = 0,
                     !.started = @ \cup {s.fifo},
                     !.status = @ \ {"cmd", "dma"}]
Arm ==
    /\ s.pc = "arm"
    /\ s' = [s EXCEPT !.masked = Polling, !.pc = "wait"]
Ready ==
    /\ \E op \in s.started \ s.ready:
        s' = [s EXCEPT !.ready = @ \cup {op},
                       !.status = @ \cup {"rb"}, !.owner["rb"] = op]
CommandDone ==
    /\ s.fsm # 0
    /\ (~WaitRB \/ s.fsm \in s.ready)
    /\ (~DMA \/ Order # "dma-first" \/ s.fsm \in s.dma)
    /\ s' = [s EXCEPT !.cmd = @ \cup {s.fsm}, !.fsm = 0,
        !.status = @ \cup {"cmd"}, !.owner["cmd"] = s.fsm]
DmaDone ==
    /\ DMA
    /\ \E op \in s.started \ s.dma:
        /\ (Order # "cmd-first" \/ op \in s.cmd)
        /\ s' = [s EXCEPT !.dma = @ \cup {op},
                          !.status = @ \cup {"dma"}, !.owner["dma"] = op]
Observe ==
    /\ s.pc = "wait" /\ (Polling \/ ~s.masked)
    /\ LET seen == s.status \cap s.remaining IN
       /\ seen # {}
       /\ s' = [s EXCEPT !.remaining = @ \ seen, !.status = @ \ seen,
                !.stale = @ \/ (\E e \in seen: s.owner[e] # s.op)]
Disarm ==
    /\ s.pc = "wait"
    /\ (s.remaining = {}
        \/ (Mutation = "fifo-is-completion" /\ s.fifo = 0)
        \/ (Mutation = "command-is-dma" /\ "cmd" \notin s.remaining))
    /\ s' = [s EXCEPT !.masked = TRUE, !.pc = "retire",
        !.status = IF Mutation = "mask-clears-status" THEN {} ELSE @]
Retire ==
    /\ s.pc = "retire"
    /\ s' = [s EXCEPT !.accepted = @ \cup {s.op},
        !.early = @ \/ s.op \notin s.cmd \/ (DMA /\ s.op \notin s.dma),
        !.op = @ + 1, !.pc = IF s.op = Operations THEN "done" ELSE "clear"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Clear \/ Enqueue \/ Latch \/ Arm \/ Ready \/ CommandDone \/
        DmaDone \/ Observe \/ Disarm \/ Retire \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
FreshCompletion == ~s.stale
CompletedBeforeReuse == ~s.early
ReadyBeforeCommand == WaitRB => s.cmd \subseteq s.ready
RbLatchPreserved == "rb" \in s.status
AcceptedExactly == s.accepted = 1..(s.op - 1)
Terminates == <> (s.pc = "done")
=============================================================================
