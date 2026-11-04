---------------------------- MODULE NandCommand ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Parser uses 1024-byte limits, independently of the 8192-byte H616 SRAM.
\* NDFC_CNT[9:0] counts bytes; zero encodes 1024, NOT an empty transfer.
\* DataIdentity tracks transferred bytes. Bytes copied from an unfilled SRAM
\* suffix cannot extend that valid prefix. The C fixture checks the stale bytes.
\* Address helper returns a count, whereas the submitted loop compares it to
\* j+start. Nonzero starts are an explicit source-contract witness.
EXTENDS Integers, Sequences
CONSTANTS Size, SRAM, Parser, Read, ReadyBusy, CheckOnly, FailChunk,
          AddrStart, AddrCount, Mutation
VARIABLE s
vars == <<s>>
Min(a,b) == IF a < b THEN a ELSE b
Input == [i \in 1..Size |-> i]
Limit == IF Parser \/ Mutation = "cap-count" THEN Min(1024, SRAM) ELSE SRAM
HwCount(n) == IF n % 1024 = 0 THEN 1024 ELSE n % 1024
Init == s = [pc |-> "address", pos |-> 0, chunks |-> 0, n |-> 0,
    address |-> <<>>, data |-> <<>>, sram |-> <<>>, issued |-> 0,
    effects |-> 0, ready |-> FALSE, failed |-> FALSE, premature |-> FALSE,
    unstaged |-> FALSE]
Address ==
    /\ s.pc = "address"
    /\ LET count == IF AddrCount > AddrStart THEN Min(8, AddrCount - AddrStart) ELSE 0
       IN s' = [s EXCEPT !.address = [i \in 1..count |-> AddrStart + i],
            !.pc = IF CheckOnly THEN "done" ELSE "prepare"]
Prepare ==
    /\ s.pc = "prepare" /\ s.pos < Size
    /\ s' = [s EXCEPT !.n = Min(Limit, Size - s.pos), !.chunks = @ + 1,
        !.ready = ~ReadyBusy, !.pc = IF Read \/ Mutation = "skip-stage"
                                       THEN "issue" ELSE "stage"]
Stage ==
    /\ s.pc = "stage"
    /\ LET start == IF Mutation = "ignore-offset" THEN 0 ELSE s.pos
       IN s' = [s EXCEPT !.sram = SubSeq(Input, start + 1, start + s.n),
                          !.effects = @ + 1, !.pc = "issue"]
Issue ==
    /\ s.pc = "issue"
    /\ LET start == IF Mutation = "ignore-offset" THEN 0 ELSE s.pos
       IN s' = [s EXCEPT !.sram = IF Read THEN SubSeq(Input, start + 1, start + HwCount(s.n)) ELSE @,
            !.unstaged = @ \/ (~Read /\ Len(s.sram) # s.n),
            !.issued = @ + 1, !.effects = @ + 1, !.pc = "wait"]
Ready ==
    /\ s.pc = "wait" /\ ~s.ready
    /\ s' = [s EXCEPT !.ready = TRUE]
Complete ==
    /\ s.pc = "wait" /\ (s.ready \/ Mutation = "skip-ready")
    /\ s' = IF s.chunks = FailChunk THEN
        [s EXCEPT !.failed = TRUE, !.pc = "done"] ELSE
        [s EXCEPT !.data = @ \o SubSeq(s.sram, 1, HwCount(s.n)), !.pos = @ + s.n,
            !.premature = @ \/ ~s.ready,
            !.pc = IF s.pos + s.n = Size THEN "done" ELSE "prepare"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Address \/ Prepare \/ Stage \/ Issue \/ Ready \/ Complete \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
AddressCount == s.pc # "address" => Len(s.address) = AddrCount
AddressIdentity == s.pc # "address" =>
    s.address = [i \in 1..AddrCount |-> AddrStart + i]
DataIdentity == s.data = SubSeq(Input, 1, s.pos)
Bounds == s.pos <= Size /\ s.n <= SRAM /\ (Parser => s.n <= 1024)
ReadAfterReady == ~s.premature
WriteBeforeIssue == ~s.unstaged
CheckOnlyPure == CheckOnly => s.effects = 0 /\ s.issued = 0
StopOnError == s.failed => s.chunks = FailChunk /\ s.issued = FailChunk
Terminates == <> (s.pc = "done")
=============================================================================
