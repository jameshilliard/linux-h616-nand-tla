--------------------------- MODULE NandReadRetry ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
EXTENDS Integers, FiniteSets
CONSTANTS N, First, Deferred, Mutation
VARIABLE s
vars == <<s>>
\* One programmed, non-erased page. DMA may transfer an unrequested prefix.
\* Media errors count towards failed; a negative transport return is separate.
ASSUME /\ First \in 1..N /\ (Deferred => First = 1)
Base == [corrected |-> 11, failed |-> 7]
Empty == [corrected |-> 0, failed |-> 0]
IoError == -1
ResetError == -2
Value(i, mode) == IF i = 2 THEN -1 ELSE IF mode = "dma" THEN i + 8 ELSE i
Max(a, b) == IF a > b THEN a ELSE b
Count(r, v) == IF v < 0 THEN [r EXCEPT !.failed = @ + 1]
               ELSE [r EXCEPT !.corrected = @ + v]
RECURSIVE Corrections(_, _), Peak(_, _)
Corrections(i, mode) == IF i < First THEN 0
    ELSE Corrections(i-1, mode) + Max(Value(i, mode), 0)
Peak(i, mode) == IF i < First THEN 0 ELSE Max(Peak(i-1, mode), Value(i, mode))
Expected(i, mode) == [corrected |-> Base.corrected + Corrections(i, mode),
    failed |-> Base.failed + Cardinality({j \in First..i : Value(j, mode) < 0})]

Init == s = [pc |-> "open", mode |-> "dma", i |-> 1, closed |-> FALSE,
             stats |-> Base, local |-> Empty, peak |-> 0, result |-> -3]
Open ==
    /\ s.pc = "open"
    /\ s' = [s EXCEPT !.pc = "read"]
OpenError ==
    /\ s.pc = "open"
    /\ s' = [s EXCEPT !.pc = "done", !.result = IoError]
Step ==
    /\ s.pc = "read" /\ s.i <= N
    /\ LET v == Value(s.i, s.mode)
           take == s.i >= First
       IN s' = [s EXCEPT !.i = @ + 1,
          !.local = IF take THEN Count(@, v) ELSE @,
          !.stats = IF take /\ ~Deferred THEN Count(@, v) ELSE @,
          !.peak = IF take THEN Max(@, v) ELSE @]
\* Hardware can fail to recover after a DMA timeout. A failure during the
\* final OOB read can be retried but does not by itself close the controller.
DmaError ==
    /\ s.pc = "read" /\ s.mode = "dma"
    /\ \E closed \in (IF s.i <= N THEN BOOLEAN ELSE {FALSE}):
          s' = [s EXCEPT !.pc = "discard", !.closed = closed]
PioError ==
    /\ s.pc = "read" /\ s.mode = "pio"
    /\ s' = [s EXCEPT !.pc = "done", !.result = IoError]
Discard ==
    /\ s.pc = "discard"
    /\ s' = [s EXCEPT
          !.stats.corrected = IF Mutation = "keep-dma-corrected" THEN @ ELSE Base.corrected,
          !.stats.failed = IF Mutation = "keep-dma-failed" THEN @ ELSE Base.failed,
          !.local = Empty, !.peak = IF Mutation = "reuse-dma-peak" THEN @ ELSE 0,
          !.pc = "dispatch"]
Retry ==
    /\ s.pc = "dispatch" /\ (~s.closed \/ Mutation = "retry-closed-read")
    /\ s' = [s EXCEPT !.pc = "open", !.mode = "pio", !.i = First]
Closed ==
    /\ s.pc = "dispatch" /\ s.closed
    /\ s' = [s EXCEPT !.pc = "done", !.result = ResetError]
Finish ==
    /\ s.pc = "read" /\ s.i = N + 1
    /\ s' = [s EXCEPT !.pc = "done", !.result = s.peak,
           !.stats.corrected = IF Deferred THEN @ + s.local.corrected ELSE @,
           !.stats.failed = IF Deferred THEN @ + s.local.failed ELSE @]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Open \/ OpenError \/ Step \/ DmaError \/ PioError \/ Discard \/ Retry \/ Closed \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
DiscardBeforeDecision == s.pc = "dispatch" => s.stats = Base
NoClosedRetry == s.closed => s.mode = "dma"
Success == s.pc = "done" /\ s.result \in Nat =>
    s.stats = Expected(N, s.mode) /\ s.result = Peak(N, s.mode)
Error == s.pc = "done" /\ s.result \notin Nat =>
    /\ s.stats = IF s.mode = "pio" /\ ~Deferred THEN Expected(s.i-1, "pio") ELSE Base
    /\ (s.result = ResetError) = s.closed
Terminates == <> (s.pc = "done")
=============================================================================
