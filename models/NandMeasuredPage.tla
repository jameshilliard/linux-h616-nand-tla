----------------------- MODULE NandMeasuredPage -----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Whole-request composition of measured H616 NORMAL/block-mode transfers.
\* Protected data/user/parity are an atomic codeword contract; BCH algebra is
\* checked separately. NORMAL keys are symbolic, independently replayed at
\* bit level by the capture and actual-source runners. Chunk=8 represents a
\* scaled command limit; byte-count modulo four is deliberately preserved.
EXTENDS Integers, Sequences
CONSTANTS Size, WriteChunk, ReadChunk, FormatChunk, PageMode, SoftwareRead,
          SoftwareChunk, Fault, Mutation
VARIABLE s
vars == <<s>>
Min(a,b) == IF a < b THEN a ELSE b
Fail(kind) == Fault = kind /\ s.request = 1
Protected == <<<<s.request,"data0">>, <<s.request,"user0">>,
               <<s.request,"parity0">>, <<s.request,"data1">>,
               <<s.request,"user1">>, <<s.request,"parity1">>>>
LogicalTail == [i \in 1..Size |-> <<s.request,i>>]
Init == s = [pc |-> "stage", request |-> 1, pos |-> 0, staged |-> <<>>,
    media |-> <<>>, protected |-> <<>>, prefix |-> <<>>, returned |-> <<>>,
    confirmed |-> FALSE, error |-> FALSE, retlen |-> 0, result |-> "pending",
    badSuccess |-> FALSE, badFormat |-> FALSE, badCopy |-> FALSE,
    badContext |-> FALSE, measured |-> TRUE, outcomes |-> <<>>]
StageProtected == /\ s.pc = "stage"
    /\ s' = [s EXCEPT !.protected = Protected, !.pc = "write"]
WriteTail == /\ s.pc = "write" /\ s.pos < Size
    /\ IF Fail("write-tail") /\ s.pos > 0 THEN
        s' = [s EXCEPT !.error = TRUE, !.pc = "finish"]
       ELSE LET n == Min(WriteChunk,Size-s.pos)
                phase == IF Mutation = "writer-continuous" THEN s.pos ELSE 0
                bytes == [i \in 1..n |-> <<s.request,s.pos+i,phase+i>>]
            IN s' = [s EXCEPT !.staged = @ \o bytes, !.pos = @+n,
                !.measured = @ /\ phase=0]
Confirm == /\ s.pc = "write" /\ s.pos = Size
    /\ s' = [s EXCEPT !.confirmed = TRUE, !.media = s.staged,
        !.badFormat = @ \/ (\E i \in 1..Size:
                           s.staged[i][3] # ((i-1)%FormatChunk)+1),
        !.error = Fail("confirm-timeout"),
        !.pc = IF Fail("confirm-timeout") THEN "finish" ELSE "read-prefix",
        !.pos = 0]
ReadProtected == /\ s.pc = "read-prefix"
    /\ LET prefix == IF Mutation = "reuse-old-context" /\ s.request=2
                     THEN <<<<1,"data0">>>> ELSE s.protected
       IN s' = [s EXCEPT !.prefix = prefix, !.badContext = @ \/ prefix # Protected,
                    !.pc = "read"]
ReadTail == /\ s.pc = "read" /\ s.pos < Size
    /\ LET n == Min(ReadChunk,Size-s.pos)
           phase == IF PageMode THEN s.pos ELSE 0
           origin == IF Mutation = "decode-from-raw-column" THEN 1 ELSE 0
           keys == [i \in 1..n |->
               IF SoftwareRead THEN
                   IF SoftwareChunk > 0 /\ Mutation # "decode-continuously"
                   THEN ((s.pos+i-1+origin)%SoftwareChunk)+1 ELSE s.pos+i
               ELSE IF n%4 # 0 /\ i=n /\ Mutation # "xor-read-tail"
                    THEN 0 ELSE phase+i]
           observed == [i \in 1..n |-> IF ~SoftwareRead /\ n%4 # 0 /\ i=n THEN 0 ELSE phase+i]
           decoded == [i \in 1..n |-> IF s.media[s.pos+i][3]=keys[i]
                        THEN <<s.media[s.pos+i][1],s.media[s.pos+i][2]>> ELSE <<0,0>>]
       IN IF Fail("read-tail") /\ s.pos+n=Size THEN
           s' = [s EXCEPT !.error = TRUE, !.pc = "finish",
               !.badCopy = Mutation = "copy-failed-tail"]
          ELSE s' = [s EXCEPT !.returned = @ \o decoded, !.pos = @+n,
               !.measured = @ /\ (SoftwareRead \/ keys=observed)]
Finish == /\ ((s.pc = "read" /\ s.pos=Size) \/ s.pc="finish")
    /\ LET success == ~s.error
       IN s' = [s EXCEPT !.result = IF success THEN "ok" ELSE "error",
            !.retlen = IF success THEN Size ELSE 0,
            !.badSuccess = @ \/ (success /\ (s.prefix # Protected \/ s.returned # LogicalTail)),
            !.outcomes = Append(@,IF success THEN "ok" ELSE "error"), !.pc = "next"]
Reuse == /\ s.pc = "next" /\ s.request = 1
    /\ s' = [s EXCEPT !.request = 2, !.pos = 0, !.staged = <<>>, !.media = <<>>,
        !.protected = <<>>, !.prefix = <<>>, !.returned = <<>>, !.confirmed = FALSE,
        !.error = FALSE, !.retlen = 0, !.result = "pending", !.pc = "stage"]
Done == s.pc="next" /\ s.request=2 /\ UNCHANGED vars
Next == StageProtected \/ WriteTail \/ Confirm \/ ReadProtected \/ ReadTail \/ Finish \/ Reuse \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
MeasuredContract == s.measured
StoredFormat == ~s.badFormat
RoundTrip == ~s.badSuccess
FailedReadNotCopied == ~s.badCopy
ContextFresh == ~s.badContext
NoPartialConfirm == s.confirmed => Len(s.media)=Size
ErrorLength == s.result="error" => s.retlen=0
Bounds == Len(s.staged)<=Size /\ Len(s.returned)<=Size /\ Len(s.outcomes)<=2
Terminates == <> (s.pc="next" /\ s.request=2)
=============================================================================
