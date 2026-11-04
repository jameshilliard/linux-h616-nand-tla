--------------------- MODULE NandRecoveryRegisters ---------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Refinement of NandRecovery: reset retention, a second selected chip and
\* outstanding writes/events are independent. Quarantine is a candidate
\* ownership policy, NOT an implementation claimed to exist in sunxi NAND.
EXTENDS Integers
CONSTANTS Backend, StopOK, RetainRegisters, RetireWrites, RetireEvents,
          Policy, NextChip, Mutation
VARIABLE s
vars == <<s>>
Dma == Backend # "pio"
Internal == Backend \in {"register", "descriptor"}
Init == s = [pc |-> "timeout", mapped |-> Dma, producer |-> Dma,
    write |-> Dma, future |-> TRUE, event |-> 0, epoch |-> 1,
    regs |-> [chip |-> 1, timing |-> 1, spare |-> 1, ecc |-> 1],
    cachedClock |-> 1, clock |-> 1, failed |-> FALSE, unsafe |-> FALSE,
    stale |-> FALSE, badContext |-> FALSE, reset |-> FALSE, terminated |-> FALSE]
LateWrite == /\ s.write
    /\ s' = [s EXCEPT !.unsafe = @ \/ ~s.mapped, !.write = FALSE]
LateEvent == /\ s.future
    /\ s' = [s EXCEPT !.future = FALSE, !.event = 1]
Timeout == /\ s.pc = "timeout"
    /\ s' = [s EXCEPT !.event = 0, !.pc = "abort"]
Abort == /\ s.pc = "abort"
    /\ s' = [s EXCEPT !.reset = Internal,
        !.terminated = Dma /\ ~Internal, !.failed = Dma /\ ~StopOK,
        !.producer = IF StopOK THEN FALSE ELSE @,
        !.write = IF StopOK /\ RetireWrites THEN FALSE ELSE @,
        !.future = IF StopOK /\ RetireEvents THEN FALSE ELSE @,
        !.event = IF StopOK /\ RetireEvents THEN 0 ELSE @,
        !.regs = IF Internal /\ StopOK /\ ~RetainRegisters
                 THEN [chip |-> 0, timing |-> 0, spare |-> 0, ecc |-> 0] ELSE @,
        !.pc = "cleanup"]
Cleanup == /\ s.pc = "cleanup"
    /\ s' = IF Policy = "quarantine" /\ s.failed
        THEN [s EXCEPT !.pc = "quarantined"]
        ELSE [s EXCEPT !.unsafe = @ \/ (s.mapped /\ (s.producer \/ s.write)),
              !.mapped = FALSE, !.pc = "select"]
Select == /\ s.pc = "select"
    /\ s' = [s EXCEPT !.clock = NextChip, !.cachedClock = NextChip,
        !.regs = IF Mutation = "skip-same-clock" /\ s.cachedClock = NextChip
                 THEN @ ELSE [chip |-> NextChip, timing |-> NextChip,
                              spare |-> NextChip, ecc |-> NextChip],
        !.epoch = 2, !.pc = "transfer"]
Transfer == /\ s.pc = "transfer"
    /\ s' = [s EXCEPT !.badContext = s.regs # [chip |-> NextChip,
        timing |-> NextChip, spare |-> NextChip, ecc |-> NextChip],
        !.stale = s.event = 1, !.pc = "settle"]
Observe == /\ s.pc = "settle" /\ s.event = 1
    /\ s' = [s EXCEPT !.stale = TRUE, !.event = 0]
Finish == /\ s.pc = "settle" /\ ~s.future /\ s.event = 0 /\ ~s.write
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc \in {"done", "quarantined"} /\ UNCHANGED vars
Next == LateWrite \/ LateEvent \/ Timeout \/ Abort \/ Cleanup \/ Select \/
        Transfer \/ Observe \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next) /\ WF_vars(LateWrite) /\ WF_vars(LateEvent)
MappingSafe == ~s.unsafe
FreshContext == ~s.badContext
NoStaleEvent == ~s.stale
CorrectBackend == s.pc \notin {"timeout", "abort"} =>
    s.reset = Internal /\ s.terminated = (Dma /\ ~Internal)
FailedStopRetained == Policy = "quarantine" /\ s.pc = "quarantined" => s.mapped
Terminates == <> (s.pc \in {"done", "quarantined"})
=============================================================================
