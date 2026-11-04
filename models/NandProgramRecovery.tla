---------------------- MODULE NandProgramRecovery ----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Separate host DMA, NAND page buffer, PROGRAM CONFIRM and array programming.
\* Fault timing and torn-array scope are NAND-part contracts, not measured
\* H616 write-abort guarantees. The current source never retries a partial
\* write; this model also checks ambiguous confirm acceptance and reuse.
EXTENDS Integers, Sequences, FiniteSets
CONSTANTS DMA, Fault, StopRetires, ResetOK, NeighborStable, Mutation
VARIABLE s
vars == <<s>>
Pieces == {"data0", "user0", "parity0", "data1", "user1", "parity1", "tail"}
First == {"data0", "user0", "parity0"}
Second == {"data1", "user1", "parity1"}
Init == s = [pc |-> "prepare", mapped |-> FALSE, dma |-> FALSE,
    pendingBus |-> FALSE, staged |-> {}, media |-> {}, confirms |-> 0,
    accepted |-> FALSE, confirmData |-> {}, busy |-> FALSE, error |-> FALSE, success |-> FALSE,
    uncertain |-> FALSE, aborted |-> FALSE, reset |-> FALSE,
    unsafe |-> FALSE, returned |-> FALSE, statusChecked |-> FALSE,
    neighboringPage |-> {"old"}, readback |-> {}, beforeReset |-> {}, replays |-> 0]
Prepare == /\ s.pc = "prepare"
    /\ s' = IF Fault = "prepare" THEN [s EXCEPT !.pc = "error", !.error = TRUE]
        ELSE [s EXCEPT !.mapped = DMA, !.dma = DMA,
            !.pendingBus = DMA, !.pc = "first"]
FirstStep == /\ s.pc = "first"
    /\ s' = [s EXCEPT !.staged = First,
        !.pc = IF Fault = "first" THEN "abort" ELSE "second"]
SecondStep == /\ s.pc = "second"
    /\ s' = [s EXCEPT !.staged = @ \cup Second,
        !.pc = IF Fault = "second" THEN "abort" ELSE "retire"]
Retire == /\ s.pc = "retire"
    /\ s' = [s EXCEPT !.dma = FALSE, !.pendingBus = FALSE,
        !.mapped = FALSE, !.pc = IF Mutation="confirm-before-tail" THEN "confirm" ELSE "tail"]
TransferTail == /\ s.pc = "tail"
    /\ s' = [s EXCEPT !.staged = @ \cup {"tail"},
        !.pc = IF Fault = "tail" THEN "error" ELSE "confirm",
        !.error = Fault = "tail"]
Confirm == /\ s.pc = "confirm"
    /\ s' = [s EXCEPT !.confirms = @ + 1, !.confirmData = s.staged,
        !.accepted = Fault # "confirm-rejected",
        !.busy = Fault # "confirm-rejected",
        !.error = Fault \in {"confirm-rejected", "confirm-accepted"},
        !.uncertain = Fault = "confirm-accepted",
        !.pc = IF Fault = "confirm-rejected" THEN "error" ELSE "program"]
Program == /\ s.pc = "program"
    /\ IF Fault \in {"power-loss", "busy-timeout", "confirm-accepted", "status-fail"}
        THEN \E written \in SUBSET s.staged:
            s' = [s EXCEPT !.media = @ \cup written, !.error = TRUE,
                !.uncertain = TRUE,
                !.neighboringPage = IF NeighborStable THEN @ ELSE {},
                !.busy = Fault \notin {"power-loss", "status-fail"},
                !.pc = IF Fault = "status-fail" THEN "status" ELSE "abort"]
        ELSE s' = [s EXCEPT !.media = s.staged, !.busy = FALSE, !.pc = "status"]
Status == /\ s.pc = "status"
    /\ s' = IF Fault = "status-fail" THEN
        [s EXCEPT !.statusChecked = TRUE, !.error = TRUE, !.pc = "error"]
        ELSE [s EXCEPT !.statusChecked = TRUE, !.success = TRUE,
            !.returned = TRUE, !.pc = "read"]
Abort == /\ s.pc = "abort"
    /\ s' = [s EXCEPT !.aborted = TRUE, !.error = TRUE,
        !.dma = IF StopRetires THEN FALSE ELSE @,
        !.pendingBus = IF StopRetires THEN FALSE ELSE @,
        !.pc = "cleanup"]
Cleanup == /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.unsafe = @ \/ (s.mapped /\ (s.dma \/ s.pendingBus)),
        !.mapped = FALSE,
        !.pc = IF Mutation = "replay-partial-write" THEN "replay" ELSE "error"]
Replay == /\ s.pc = "replay"
    /\ s' = [s EXCEPT !.replays = @ + 1, !.pc = "error"]
Error == /\ s.pc = "error"
    /\ s' = [s EXCEPT !.returned = TRUE,
        !.success = Mutation = "timeout-is-success", !.pc = "reset"]
ResetChip == /\ s.pc = "reset"
    /\ s' = IF ResetOK THEN
        [s EXCEPT !.reset = TRUE, !.busy = FALSE, !.staged = {},
            !.beforeReset = s.media,
            !.media = IF Mutation="reset-rolls-back" THEN {} ELSE @, !.pc = "read"]
        ELSE [s EXCEPT !.pc = "closed"]
Read == /\ s.pc = "read" /\ ~s.busy
    /\ s' = [s EXCEPT !.readback = s.media, !.pc = "done"]
Done == s.pc \in {"done", "closed"} /\ UNCHANGED vars
Next == Prepare \/ FirstStep \/ SecondStep \/ Retire \/ TransferTail \/ Confirm \/
        Program \/ Status \/ Abort \/ Cleanup \/ Replay \/ Error \/ ResetChip \/ Read \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
MappingSafe == ~s.unsafe
NoWriteReplay == s.replays = 0 /\ s.confirms <= 1
ConfirmAfterData == s.accepted => s.confirms = 1 /\ s.confirmData = Pieces /\ ~s.mapped
SuccessDurable == s.success => s.statusChecked /\ s.media = Pieces /\ ~s.busy
NoProgramBeforeConfirm == s.media # {} => s.accepted
NeighborPreserved == s.neighboringPage = {"old"}
ReadObservesMedia == s.pc = "done" => s.readback = s.media
NoRollbackClaim == /\ s.reset => s.media=s.beforeReset
                   /\ s.reset /\ s.accepted => s.uncertain
Terminates == <> (s.pc \in {"done", "closed"})
=============================================================================
