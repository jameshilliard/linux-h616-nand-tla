---------------------------- MODULE NandTransfer ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, FiniteSets
CONSTANTS Write, Batches, Mutation, AbortStops
\* Transport errors are distinct from ECC uncorrectability, which is handled
\* in NandEcc. A single injected transport failure suffices to exercise every
\* unwind edge; the subsequent PIO attempt is allowed to succeed.
VARIABLE s
vars == <<s>>
Init == s =
    [pc |-> "setup", batch |-> 0, mode |-> "dma", mapped |-> FALSE,
     active |-> FALSE, ecc |-> FALSE, random |-> FALSE,
     faultUsed |-> FALSE, failed |-> FALSE, terminalError |-> FALSE,
     begins |-> 0, confirms |-> 0, issued |-> 0, corrected |-> 7,
     fallback |-> FALSE, replayedWrite |-> FALSE, unsafeUnmap |-> FALSE,
     prematureConfirm |-> FALSE]
Setup ==
    /\ s.pc = "setup"
    /\ s' = [s EXCEPT !.pc = IF Write THEN "prepare" ELSE "read-begin"]
ReadBegin ==
    /\ s.pc = "read-begin"
    /\ s' = [s EXCEPT !.pc = "prepare"]
ReadBeginError ==
    /\ s.pc = "read-begin" /\ ~s.faultUsed
    /\ s' = [s EXCEPT !.pc = "done", !.faultUsed = TRUE, !.terminalError = TRUE]
Prepare ==
    /\ s.pc = "prepare"
    /\ s' = [s EXCEPT !.mapped = s.mode = "dma", !.pc = "begin"]
PrepareError ==
    /\ s.pc = "prepare" /\ ~s.faultUsed
    /\ s' = [s EXCEPT !.faultUsed = TRUE, !.failed = TRUE, !.pc =
         IF ~Write \/ s.batch = 0 \/ Mutation = "partial-write-retry"
         THEN "fallback" ELSE "error"]
Begin ==
    /\ s.pc = "begin"
    /\ s' = [s EXCEPT !.pc = "transfer",
         !.begins = IF Write /\ s.batch = 0 THEN @ + 1 ELSE @,
         !.ecc = TRUE, !.random = TRUE]
BeginError ==
    /\ s.pc = "begin" /\ ~s.faultUsed
    /\ s' = [s EXCEPT !.faultUsed = TRUE, !.failed = TRUE, !.pc = "abort"]
Transfer ==
    /\ s.pc = "transfer"
    /\ s' = [s EXCEPT !.active = s.mode = "dma", !.issued = @ + 1,
                     !.pc = "wait"]
FinishTransfer ==
    /\ s.pc = "wait"
    /\ s' = [s EXCEPT !.active = FALSE, !.pc = "cleanup"]
TransferError ==
    /\ s.pc = "wait" /\ ~s.faultUsed
    /\ s' = [s EXCEPT !.faultUsed = TRUE, !.failed = TRUE, !.pc = "abort"]
Abort ==
    /\ s.pc = "abort"
    /\ s' = [s EXCEPT !.active = IF AbortStops THEN FALSE ELSE @,
                     !.pc = "cleanup"]
Cleanup ==
    /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.unsafeUnmap = @ \/ (s.mapped /\ s.active),
         !.mapped = FALSE, !.ecc = Mutation = "leak-ecc",
         !.random = FALSE,
         !.pc = IF s.failed THEN IF Write THEN "error" ELSE "fallback"
                ELSE "consume"]
Consume ==
    /\ s.pc = "consume"
    /\ s' = [s EXCEPT !.corrected = IF Write THEN @ ELSE @ + 1,
         !.batch = @ + 1,
         !.pc = IF s.batch + 1 = Batches THEN "tail" ELSE "prepare"]
ConsumeError ==
    /\ s.pc = "consume" /\ ~Write /\ ~s.faultUsed
    /\ s' = [s EXCEPT !.faultUsed = TRUE, !.failed = TRUE, !.pc = "fallback"]
Tail ==
    /\ s.pc = "tail"
    /\ s' = [s EXCEPT !.pc = IF Write THEN "confirm" ELSE "done"]
TailError ==
    /\ s.pc = "tail" /\ ~s.faultUsed
    /\ s' = [s EXCEPT !.faultUsed = TRUE, !.failed = TRUE,
                     !.pc = IF Write THEN "error" ELSE "fallback"]
Fallback ==
    /\ s.pc = "fallback"
    /\ s' = [s EXCEPT !.replayedWrite = @ \/ (Write /\ s.issued > 0),
         !.mode = "pio", !.batch = 0, !.failed = FALSE, !.fallback = TRUE,
         !.corrected = IF Mutation = "keep-partial-stats" THEN @ ELSE 7,
         !.pc = "prepare"]
Error ==
    /\ s.pc = "error"
    /\ s' = [s EXCEPT !.terminalError = TRUE,
                     !.pc = IF Mutation = "confirm-on-error" THEN "confirm" ELSE "done"]
Confirm ==
    /\ s.pc = "confirm"
    /\ s' = [s EXCEPT !.confirms = @ + 1,
                     !.prematureConfirm = @ \/ s.failed \/ s.batch # Batches,
                     !.pc = IF Mutation = "oob-double-confirm" THEN "wrapper-confirm"
                            ELSE "done"]
WrapperConfirm ==
    /\ s.pc = "wrapper-confirm"
    /\ s' = [s EXCEPT !.confirms = @ + 1, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Setup \/ ReadBegin \/ ReadBeginError \/ Prepare \/ PrepareError \/
        Begin \/ BeginError \/ Transfer \/ FinishTransfer \/ TransferError \/
        Abort \/ Cleanup \/ Consume \/ ConsumeError \/ Tail \/ TailError \/
        Fallback \/ Error \/ Confirm \/ WrapperConfirm \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
NoUnsafeUnmap == ~s.unsafeUnmap
NoWriteReplay == ~s.replayedWrite
OneConfirm == s.confirms <= 1
NoPrematureConfirm == ~s.prematureConfirm
ReturnCleanup == s.pc = "done" => ~s.mapped /\ ~s.ecc /\ ~s.random
ReadStats == ~Write /\ s.pc = "done" /\ ~s.terminalError =>
    s.corrected = 7 + Batches
WriteSuccess == Write /\ s.pc = "done" /\ ~s.terminalError =>
    s.begins = 1 /\ s.confirms = 1
Terminates == <> (s.pc = "done")
=============================================================================
