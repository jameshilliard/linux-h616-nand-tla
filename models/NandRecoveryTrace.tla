--------------------------- MODULE NandRecoveryTrace --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Finite source-helper trace conformance, not a whole-C refinement proof.
\* The event schedule and the DMA/bus retirement contracts are fixture inputs.
EXTENDS Integers, Sequences, FiniteSets, TLCExt
CONSTANT Traces
VARIABLE s
vars == <<s>>
Bits(n) == {b \in {1, 2, 4} : (n \div b) % 2 = 1}
Init == \E t \in 1..Len(Traces) :
    s = [t |-> t, pos |-> 1, phase |-> "initial", epoch |-> 0,
         mapped |-> FALSE, active |-> FALSE, bus |-> FALSE,
         mask |-> {}, status |-> {}, complete |-> FALSE, disabled |-> FALSE,
         maps |-> 0, unmaps |-> 0, direction |-> -1, selected |-> -1,
         seen |-> {}, accepted |-> FALSE, lastRet |-> 0, stopper |-> "none",
         protocol |-> TRUE, snapshots |-> TRUE, context |-> TRUE,
         irqClean |-> TRUE, cleanup |-> TRUE, accounting |-> TRUE,
         mappingSafe |-> TRUE, fresh |-> TRUE, done |-> FALSE]
\* Each input trace is immutable. Cache by its index, never by mutable state.
\* TLCCache is the identity operator mathematically; the pinned TLC provides
\* the evaluation cache so repeated snapshot checks do not rebuild literals.
Input == TLCCache(Traces[s.t], s.t)
Req == IF Input.backend = 0 THEN {2} ELSE {2, 4}
Advance ==
    /\ ~s.done /\ s.pos <= Len(Input.events)
    /\ LET e == Input.events[s.pos]
           k == e.kind
           bits == Bits(e.arg)
           arrived == k = "arrive" /\ Req \subseteq (s.seen \cup bits)
           stopped == k \in {"reset-ok", "terminate-ok"}
           mapped == CASE k = "map" -> TRUE [] k = "unmap" -> FALSE [] OTHER -> s.mapped
           active == CASE k = "launch" -> TRUE [] arrived \/ stopped -> FALSE [] OTHER -> s.active
           bus == CASE k = "launch" -> TRUE
                    [] arrived -> Input.contract = 4
                    [] stopped -> Input.contract = 2 /\ s.bus
                    [] OTHER -> s.bus
           status == CASE k \in {"arrive", "arrive-old"} -> s.status \cup bits
                       [] k = "ack" -> s.status \ bits
                       [] k = "reset-ok" -> {}
                       [] k = "launch" /\ Input.backend = 2 -> s.status \ Req
                       [] OTHER -> s.status
           mask == IF k = "mask" THEN bits ELSE s.mask
           complete == CASE k = "complete" -> TRUE [] k = "reinit" -> FALSE [] OTHER -> s.complete
           disabled == CASE k = "irq-off" -> TRUE [] k = "irq-on" -> FALSE [] OTHER -> s.disabled
           maps == IF k = "map" THEN s.maps + 1 ELSE s.maps
           unmaps == IF k = "unmap" THEN s.unmaps + 1 ELSE s.unmaps
           safe == s.mappingSafe /\ (k = "unmap" => ~s.active /\ ~s.bus)
           valid == CASE k = "select" ->
                            s.phase \in {"initial", "cleaned"} /\ ~s.mapped /\
                            e.arg = (IF s.epoch = 0 THEN 0 ELSE 2 * Input.nextCs)
                     [] k = "map" -> s.phase = "ready" /\ ~s.mapped /\ e.arg = Input.direction
                     [] k = "prepare" -> s.phase = "ready" /\ s.mapped /\ e.arg = 0 /\ e.ram
                     [] k = "launch" -> s.phase = "prepared" /\ s.mapped /\ e.arg = s.epoch + 1
                     [] k = "arrive" -> s.phase \in {"active", "waiting"} /\ bits # {} /\ bits \subseteq Req
                     [] k = "arrive-old" -> Input.contract = 3 /\ s.epoch = 2 /\ s.phase = "waiting" /\ bits = Req
                     [] k = "wait-start" -> s.phase = "active" /\ bits = Req
                     [] k = "reinit" -> s.phase = "waiting"
                     [] k = "mask" -> s.phase = "waiting" /\ bits \subseteq Req
                     [] k = "ack" -> s.phase = "waiting" /\ bits \subseteq Req
                     [] k = "complete" -> s.phase = "waiting" /\ s.mask = {}
                     [] k = "irq-off" -> s.phase = "waiting" /\ ~s.disabled
                     [] k = "irq-on" -> s.phase = "waiting" /\ s.disabled
                     [] k = "wait-return" -> s.phase = "waiting" /\ e.arg \in {0, -110} /\
                            ((e.arg = 0) = s.accepted)
                     [] k = "reset-start" -> Input.backend # 0 /\
                            s.phase \in {"waited", "unmapped"} /\ s.lastRet = -110
                     [] k = "reset-ok" -> Input.backend # 0 /\ s.stopper = "reset" /\ Input.contract # 1
                     [] k \in {"terminate-ok", "terminate-error"} -> Input.backend = 0 /\
                            s.phase = "waited" /\ s.lastRet = -110 /\
                            ((k = "terminate-error") = (Input.contract = 1))
                     [] k = "abort-return" -> s.phase = "waited" /\ s.lastRet = -110 /\
                            s.stopper = (IF Input.backend = 0 THEN "external" ELSE "reset")
                     [] k = "unmap" -> s.mapped /\ e.arg = s.direction /\
                            (s.phase = "aborted" \/ (s.phase = "waited" /\ s.lastRet = 0))
                     [] k = "cleanup-return" -> s.phase = "unmapped"
                     [] k = "finish" -> s.phase = "cleaned" /\ s.pos = Len(Input.events) /\
                            e.arg = s.epoch /\ s.epoch = (IF Input.contract \in {0, 3} THEN 2 ELSE 1)
                     [] OTHER -> FALSE
       IN s' = [s EXCEPT
            !.pos = @ + 1, !.mapped = mapped, !.active = active, !.bus = bus,
            !.status = status, !.mask = mask, !.complete = complete, !.disabled = disabled,
            !.maps = maps, !.unmaps = unmaps, !.mappingSafe = safe,
            !.direction = IF k = "map" THEN e.arg ELSE @,
            !.selected = IF k = "select" THEN e.arg ELSE @,
            !.epoch = IF k = "launch" THEN @ + 1 ELSE @,
            !.seen = CASE k = "launch" -> {} [] k = "arrive" -> @ \cup bits [] OTHER -> @,
            !.accepted = CASE k = "reinit" -> FALSE [] k = "complete" -> TRUE
                           [] k = "mask" /\ e.arg = 0 /\ s.mask \subseteq s.status -> TRUE
                           [] OTHER -> @,
            !.lastRet = IF k = "wait-return" THEN e.arg ELSE @,
            !.stopper = CASE k = "launch" -> "none" [] k = "reset-start" -> "reset"
                         [] k \in {"terminate-ok", "terminate-error"} -> "external" [] OTHER -> @,
            !.phase = CASE k = "select" -> "ready" [] k = "prepare" -> "prepared"
                       [] k = "launch" -> "active" [] k = "wait-start" -> "waiting"
                       [] k = "wait-return" -> "waited" [] k = "abort-return" -> "aborted"
                       [] k = "unmap" -> "unmapped" [] k = "cleanup-return" -> "cleaned"
                       [] OTHER -> @,
            !.protocol = @ /\ valid /\ (s.pos < Len(Input.events) \/ k = "finish"),
            !.snapshots = @ /\ e.mapped = mapped /\ e.active = active /\ e.bus = bus /\
                Bits(e.mask) = mask /\ Bits(e.status) = status /\ e.complete = complete /\
                e.disabled = disabled /\ e.maps = maps /\ e.unmaps = unmaps /\ e.unsafe = ~safe,
            !.context = @ /\ (k \in {"select", "launch"} =>
                e.context = <<IF k = "select" THEN e.arg ELSE s.selected, 256, 1911, 2048, 1, 24000000>>),
            !.irqClean = @ /\ (k \in {"irq-on", "wait-return", "unmap"} => mask = {}) /\
                (k \in {"wait-return", "unmap"} => ~disabled),
            !.cleanup = @ /\ (k = "cleanup-return" => ~mapped /\ ~e.ram /\ maps = unmaps),
            !.accounting = @ /\ (k = "finish" => ~mapped /\ maps = unmaps /\ maps = s.epoch),
            !.fresh = @ /\ (k = "wait-return" /\ e.arg = 0 => Req \subseteq s.seen),
            !.done = s.pos = Len(Input.events)]
Done == s.done /\ UNCHANGED vars
Next == Advance \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Advance)
ProtocolValid == s.protocol
SnapshotsMatchC == s.snapshots
FreshContext == s.context
CleanIrqReturn == s.irqClean
HelperCleanup == s.cleanup
FinalAccounting == s.accounting
MappingSafe == s.mappingSafe
FreshCompletion == s.fresh
\* These expected outcomes depend on the requested contract, never on whether
\* the fixture happened to report an unsafe access. Check every input trace.
ContractOutcome == s.done =>
    (s.mappingSafe = (Input.contract = 0) /\ s.fresh = (Input.contract # 3))
NormalMappingSafe == Input.contract = 0 => MappingSafe
NormalFreshCompletion == Input.contract = 0 => FreshCompletion
Terminates == <> s.done
=============================================================================
