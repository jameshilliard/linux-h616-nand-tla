-------------------------- MODULE NandController --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Compose wait_events(), DMA abort/cleanup, read fallback and operation reuse.
\* Hardware completion and CPU/IRQ progress are separate actions. Reset/terminate
\* stopping DMA and suppressing old events is a contract, not a proven property.
EXTENDS Integers, FiniteSets, Sequences
CONSTANTS Operations, Batches, Write, Backend, AbortStops, CommandDrains,
          PioQuiesces, OldCommandQuiesces, PollModes, Mutation
VARIABLE s
vars == <<s>>
Events == IF s.mode = "pio" \/ Backend = "external" THEN {"cmd"}
          ELSE {"cmd", "dma"}
Polling == s.mode = "pio" /\ PollModes[s.op]
Init == s = [op |-> 1, batch |-> 1, epoch |-> 0, cpu |-> "prepare",
    mode |-> "dma", irq |-> "idle", mapped |-> FALSE, active |-> FALSE,
    busy |-> FALSE, ecc |-> FALSE, random |-> FALSE, status |-> {}, mask |-> {},
    arrived |-> {}, hs |-> {}, hi |-> {}, hgen |-> 0, done |-> FALSE,
    cgen |-> 0, disabled |-> FALSE, result |-> "unset", error |-> FALSE,
    issued |-> 0, completed |-> 0, confirms |-> 0, stats |-> 0,
    stale |-> FALSE, unsafe |-> FALSE, replay |-> FALSE, fallback |-> FALSE,
    late |-> FALSE, lateEpoch |-> 0, statusEpoch |-> 0]
Prepare ==
    /\ s.cpu = "prepare"
    /\ s' = [s EXCEPT !.mapped = s.mode = "dma", !.cpu = "issue"]
PrepareFail ==
    /\ s.cpu = "prepare" /\ s.mode = "dma"
    /\ s' = [s EXCEPT !.error = TRUE, !.cpu = "decision"]
Issue ==
    /\ s.cpu = "issue"
    /\ s' = [s EXCEPT !.epoch = @ + 1, !.cpu = "wait", !.busy = TRUE,
        !.active = s.mode = "dma", !.ecc = TRUE, !.random = TRUE,
        !.arrived = {}, !.done = FALSE, !.result = "unset",
        !.mask = IF Polling THEN {} ELSE Events, !.issued = @ + 1]
Hardware ==
    /\ s.busy
    /\ \E e \in {"cmd", "dma"} \ s.arrived :
        /\ e # "dma" \/ s.mode = "dma"
        /\ s' = [s EXCEPT !.arrived = @ \cup {e},
            !.status = IF e = "cmd" \/ Backend # "external" THEN @ \cup {e} ELSE @,
            !.statusEpoch = IF e = "cmd" THEN s.epoch ELSE @,
            !.active = IF e = "dma" \/
                (e = "cmd" /\ Backend = "external" /\ CommandDrains)
                THEN FALSE ELSE @]
LateEvent ==
    /\ s.late /\ s.cpu = "wait" /\ s.epoch > s.lateEpoch
    /\ s' = [s EXCEPT !.status = @ \cup {"cmd"}, !.statusEpoch = s.lateEpoch,
                       !.late = FALSE]
IrqRead ==
    /\ s.irq = "idle" /\ ~s.disabled /\ s.mask \cap s.status # {}
    /\ s' = [s EXCEPT !.irq = "ack", !.hs = s.status, !.hi = s.mask,
                       !.hgen = IF "cmd" \in s.status THEN s.statusEpoch ELSE s.epoch]
IrqAck ==
    /\ s.irq = "ack"
    /\ s' = [s EXCEPT !.status = @ \ s.hs, !.mask = s.hi \ s.hs,
        !.stale = @ \/ s.hgen # s.epoch, !.irq = "signal"]
IrqSignal ==
    /\ s.irq = "signal"
    /\ s' = [s EXCEPT !.done = @ \/ s.hi \subseteq s.hs,
        !.cgen = IF s.hi \subseteq s.hs THEN s.hgen ELSE @, !.irq = "idle"]
Wake ==
    /\ s.cpu = "wait" /\ ~Polling /\ s.done
    /\ s' = [s EXCEPT !.result = "ok", !.cpu = "disarm",
        !.stale = @ \/ s.cgen # s.epoch]
Poll ==
    /\ s.cpu = "wait" /\ Polling /\ Events \subseteq s.status
    /\ s' = [s EXCEPT !.result = "ok", !.cpu = "disarm",
                       !.stale = @ \/ s.statusEpoch # s.epoch]
Timeout ==
    /\ s.cpu = "wait"
    /\ s' = [s EXCEPT !.disabled = ~Polling, !.cpu = "drain"]
Drain ==
    /\ s.cpu = "drain"
    /\ s.irq = "idle" \/ Mutation = "skip-drain"
    /\ s' = [s EXCEPT !.cpu = "check"]
Check ==
    /\ s.cpu = "check"
    /\ LET pending == IF Polling THEN Events ELSE s.mask
       IN s' = [s EXCEPT !.result = IF pending \subseteq s.status
                                             THEN "ok" ELSE "timeout",
                          !.cpu = "disarm"]
Disarm ==
    /\ s.cpu = "disarm"
    /\ s' = [s EXCEPT !.mask = {}, !.status = @ \ Events, !.disabled = FALSE,
        !.cpu = IF s.result = "ok" THEN "cleanup" ELSE "abort",
        !.busy = IF s.result = "ok" THEN FALSE ELSE @]
Abort ==
    /\ s.cpu = "abort"
    /\ LET stops == IF s.mode = "dma" THEN AbortStops ELSE PioQuiesces
       IN s' = [s EXCEPT !.active = IF stops THEN FALSE ELSE @,
            !.busy = IF stops THEN FALSE ELSE @,
            !.status = IF stops THEN {} ELSE @,
            !.late = @ \/ ~OldCommandQuiesces, !.lateEpoch = s.epoch,
            !.error = TRUE, !.cpu = "cleanup"]
\* PIO has no explicit abort/reset in the driver; its quiescence is assumed.
Cleanup ==
    /\ s.cpu = "cleanup"
    /\ s' = [s EXCEPT !.unsafe = @ \/ (s.mapped /\ s.active),
        !.mapped = FALSE, !.ecc = FALSE, !.random = FALSE, !.cpu = "decision"]
Fallback ==
    /\ s.cpu = "decision" /\ s.error /\ s.mode = "dma"
    /\ ~Write \/ s.issued = 0 \/ Mutation = "replay-write"
    /\ s' = [s EXCEPT !.mode = "pio", !.batch = 1, !.completed = 0,
        !.error = FALSE, !.fallback = TRUE, !.cpu = "prepare",
        !.stats = IF Mutation = "keep-stats" THEN @ ELSE 0,
        !.replay = @ \/ (Write /\ s.issued # 0)]
Fail ==
    /\ s.cpu = "decision" /\ s.error
    /\ s.mode = "pio" \/ (Write /\ s.issued # 0 /\ Mutation # "replay-write")
    /\ s' = [s EXCEPT !.cpu = "return"]
Advance ==
    /\ s.cpu = "decision" /\ ~s.error
    /\ s' = [s EXCEPT !.completed = @ + 1, !.stats = @ + 1,
        !.cpu = IF s.batch = Batches THEN "finish" ELSE "prepare",
        !.batch = @ + 1]
Finish ==
    /\ s.cpu = "finish"
    /\ s' = [s EXCEPT !.confirms = IF Write THEN @ + 1 ELSE @, !.cpu = "return"]
Return ==
    /\ s.cpu = "return"
    /\ s' = IF s.op = Operations THEN [s EXCEPT !.cpu = "done"] ELSE
        [s EXCEPT !.op = @ + 1, !.batch = 1, !.cpu = "prepare", !.mode = "dma",
            !.error = FALSE, !.issued = 0, !.completed = 0, !.confirms = 0,
            !.stats = 0, !.fallback = FALSE]
Done == s.cpu = "done" /\ s.irq = "idle" /\ UNCHANGED vars
Cpu == Prepare \/ PrepareFail \/ Issue \/ Wake \/ Poll \/ Timeout \/ Drain \/
    Check \/ Disarm \/ Abort \/ Cleanup \/ Fallback \/ Fail \/ Advance \/ Finish \/ Return
Irq == IrqRead \/ IrqAck \/ IrqSignal
Next == Cpu \/ Irq \/ Hardware \/ LateEvent \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Cpu) /\ WF_vars(Irq)
NoUnsafeUnmap == ~s.unsafe
NoStaleIrq == ~s.stale
NoWriteReplay == ~s.replay
ConfirmOnce == s.confirms <= 1 /\ (s.confirms > 0 => s.completed = Batches /\ ~s.error)
ReadAccounting == ~Write /\ s.cpu = "finish" => s.stats = Batches
CleanReturn == s.cpu \in {"return", "done"} =>
    ~s.mapped /\ ~s.active /\ ~s.busy /\ ~s.ecc /\ ~s.random /\ s.mask = {}
Terminates == <> (s.cpu = "done" /\ s.irq = "idle")
=============================================================================
