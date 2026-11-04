--------------------------- MODULE NandDmaBytes ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Byte and cache-line refinement. Direct streaming DMA requires exclusive
\* ownership of affected cache lines, not just the nominal byte range.
\* Bounce mode isolates those lines and copies only the requested bytes.
EXTENDS Integers
CONSTANTS Bounce, Aligned, NeighborWrites, StopOK, Direction, Mutation
VARIABLE s
vars == <<s>>
Bytes == 0..7
Payload == IF Aligned THEN 0..3 ELSE 1..2
Neighbor == IF Aligned THEN 4 ELSE 0
Initial == [b \in Bytes |-> IF Direction = "tx" /\ b \in Payload THEN 5 ELSE 0]
Init == s = [pc |-> "map", cpu |-> Initial, memory |-> Initial, dma |-> Initial,
    mapped |-> FALSE, outstanding |-> FALSE, generation |-> 0,
    transferGeneration |-> 0, neighborWritten |-> FALSE, unsafe |-> FALSE,
    completed |-> FALSE, copiedComplete |-> FALSE, badTx |-> FALSE]
Map == /\ s.pc = "map"
    /\ s' = [s EXCEPT !.mapped = TRUE, !.generation = 1,
        !.memory = IF Mutation = "skip-publish" THEN [b \in Bytes |-> 0] ELSE s.cpu,
        !.dma = IF Mutation = "skip-publish" THEN [b \in Bytes |-> 0] ELSE s.cpu,
        !.transferGeneration = 1,
        !.outstanding = TRUE, !.pc = "device"]
NeighborWrite == /\ s.pc \in {"device", "stop", "unmap"}
    /\ NeighborWrites /\ ~s.neighborWritten
    /\ s' = [s EXCEPT !.cpu[Neighbor] = 9, !.neighborWritten = TRUE]
DeviceTransfer == /\ s.outstanding
    /\ s' = [s EXCEPT !.dma = IF Direction = "tx" THEN @ ELSE
            [b \in Bytes |-> IF b \in Payload THEN 7 ELSE s.dma[b]],
        !.memory = IF Bounce \/ Direction = "tx" THEN @ ELSE
            [b \in Bytes |-> IF b \in Payload THEN 7 ELSE s.memory[b]],
        !.badTx = Direction = "tx" /\
            (\E b \in Payload : (IF Bounce THEN s.dma[b] ELSE s.memory[b]) # 5),
        !.unsafe = @ \/ ~s.mapped \/ s.transferGeneration # s.generation,
        !.outstanding = FALSE, !.completed = TRUE]
Stop == /\ s.pc = "device"
    /\ s' = [s EXCEPT !.outstanding = IF StopOK THEN FALSE ELSE @, !.pc = "unmap"]
Unmap == /\ s.pc = "unmap"
    /\ s' = [s EXCEPT !.cpu = IF Direction = "tx" THEN @ ELSE [b \in Bytes |->
        IF b \in Payload THEN (IF Mutation = "skip-copyback" THEN s.cpu[b]
                               ELSE IF Bounce THEN s.dma[b] ELSE s.memory[b])
        ELSE IF b \in 0..3 /\ (~Bounce \/ Mutation = "whole-line-copyback")
             THEN s.memory[b] ELSE s.cpu[b]],
        !.copiedComplete = s.completed, !.mapped = FALSE, !.pc = "reuse"]
Reuse == /\ s.pc = "reuse"
    /\ s' = [s EXCEPT !.generation = 2, !.mapped = TRUE, !.pc = "drain"]
Finish == /\ s.pc = "drain" /\ ~s.outstanding
    /\ s' = [s EXCEPT !.mapped = FALSE, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Map \/ NeighborWrite \/ DeviceTransfer \/ Stop \/ Unmap \/ Reuse \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next) /\ WF_vars(DeviceTransfer)
NeighborPreserved == s.pc \in {"reuse", "drain", "done"} /\ s.neighborWritten => s.cpu[Neighbor] = 9
NoOldDma == ~s.unsafe
PayloadOnly == \A b \in Bytes \ (Payload \cup {Neighbor}) : s.cpu[b] = 0
DeliveredPayload == s.copiedComplete /\ Direction = "rx" =>
    \A b \in Payload : s.cpu[b] = 7
PublishedPayload == ~s.badTx
Terminates == <> (s.pc = "done")
=============================================================================
