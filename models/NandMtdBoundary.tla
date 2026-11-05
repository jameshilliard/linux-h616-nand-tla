------------------------- MODULE NandMtdBoundary -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Public MTD admission, nested translation and result/statistics projection.
\* Source policy keeps the unsigned arithmetic in pinned mtd_check_oob_ops.
\* Eof-fixed adds only the nonempty-OOB starting-page check.
\* Data-fixed uses subtraction for data bounds; both-fixed combines the fixes.
\* Bounded is the mathematical contract, including partial-page OOB bounds.
\* The callback outcome is an explicit input; no analog NAND/ECC simulation.
EXTENDS Integers, Sequences
CONSTANTS Requests, Size, PageSize, SpareSize, FreeSize, Offsets, WordMod,
          Policy, Mutation
VARIABLE s
vars == <<s>>
RECURSIVE OffsetSum(_)
OffsetSum(n) == IF n = 0 THEN 0 ELSE Offsets[n] + OffsetSum(n - 1)
Avail(q) == IF q.mode = "auto" THEN FreeSize ELSE SpareSize
Normalize(q) == [q EXCEPT !.len = IF q.data THEN @ ELSE 0,
                          !.ooblen = IF q.oob THEN @ ELSE 0]
\* Mathematical contract: a nonempty spare transfer needs at least one page.
Within(q) == /\ q.off >= 0 /\ q.off <= Size /\ q.len <= Size - q.off
    /\ (q.ooblen = 0 \/
        (q.off < Size /\ q.ooboff < Avail(q) /\
         q.ooblen + q.ooboff <= (Size \div PageSize - q.off \div PageSize) * Avail(q)))
Admit(q) ==
    IF Policy # "bounded" THEN
        /\ IF Policy \in {"data-fixed", "both-fixed"}
           THEN /\ q.off >= 0 /\ q.off <= Size /\ q.len <= Size - q.off
           ELSE /\ q.off >= 0 /\ (q.off + q.len) % WordMod <= Size
        /\ (q.ooblen = 0 \/
            ((Policy \notin {"eof-fixed", "both-fixed"} \/ q.off < Size) /\
             q.ooboff < Avail(q) /\ q.ooblen <=
             ((Size \div PageSize - q.off \div PageSize) * Avail(q) - q.ooboff) % WordMod))
    ELSE Within(q)
Init == \E q \in {Requests[i] : i \in 1..Len(Requests)} :
    s = [pc |-> "normalize", q |-> q, admitted |-> FALSE,
         retlen |-> 123, oobretlen |-> 123, result |-> "unset", address |-> q.off,
         parent |-> 1, dispatched |-> FALSE, corrected |-> 0, failed |-> 0,
         ancestors |-> [i \in 1..Len(Offsets) |-> 0],
         failedAncestors |-> [i \in 1..Len(Offsets) |-> 0],
         maxflips |-> 99, reqCorrected |-> 99, reqFailed |-> 99]
NormalizeRequest ==
    /\ s.pc = "normalize"
    /\ LET readonly == s.q.write /\ ~s.q.writable
       IN s' = [s EXCEPT !.q = IF readonly THEN @ ELSE Normalize(s.q),
        !.pc = IF readonly THEN "done" ELSE "validate",
        !.result = IF readonly THEN "ro" ELSE @,
        !.retlen = IF Mutation = "stale-lengths" THEN @ ELSE 0,
        !.oobretlen = IF Mutation = "stale-lengths" THEN @ ELSE 0]
Validate ==
    /\ s.pc = "validate"
    /\ LET ok == Admit(s.q)
           reset == s.q.stats /\ ok /\ (~s.q.write \/ Mutation = "reset-write-stats")
       IN s' = [s EXCEPT !.admitted = ok,
           !.pc = IF ok THEN "translate" ELSE "done",
           !.result = IF ok THEN "unset" ELSE "invalid",
           !.maxflips = IF reset THEN 0 ELSE @,
           !.reqCorrected = IF reset THEN 0 ELSE @,
           !.reqFailed = IF reset THEN 0 ELSE @]
Translate ==
    /\ s.pc = "translate" /\ s.parent <= Len(Offsets)
    /\ s' = [s EXCEPT !.address = @ +
        (IF Mutation = "skip-parent" /\ s.parent = Len(Offsets) THEN 0 ELSE Offsets[s.parent]),
        !.parent = @ + 1]
Dispatch ==
    /\ s.pc = "translate" /\ s.parent > Len(Offsets)
    /\ s' = [s EXCEPT !.pc = "account", !.dispatched = TRUE,
        !.retlen = s.q.dataret, !.oobretlen = s.q.oobret,
        !.corrected = IF s.q.write THEN 0 ELSE s.q.corrected,
        !.failed = IF s.q.write THEN 0 ELSE s.q.failed,
        !.reqCorrected = IF s.q.stats /\ ~s.q.write THEN s.q.corrected ELSE @,
        !.reqFailed = IF s.q.stats /\ ~s.q.write THEN s.q.failed ELSE @]
Account ==
    /\ s.pc = "account"
    /\ s' = [s EXCEPT !.pc = "finish",
        !.ancestors = [i \in 1..Len(Offsets) |->
            IF Mutation = "lost-ancestor-stats" /\ i = Len(Offsets) THEN 0 ELSE s.corrected],
        !.failedAncestors = [i \in 1..Len(Offsets) |->
            IF Mutation = "lost-failed-stats" THEN 0 ELSE s.failed]]
Finish ==
    /\ s.pc = "finish"
    /\ s' = [s EXCEPT !.pc = "done",
        !.maxflips = IF s.q.stats /\ ~s.q.write /\ s.q.ecc /\ s.q.error = "none"
                    THEN s.q.flips ELSE @,
        !.result = IF s.q.error # "none" THEN
                       IF Mutation = "hide-error" THEN "ok" ELSE s.q.error
                   ELSE IF ~s.q.write /\ s.q.ecc /\
                       (IF Mutation = "threshold-off-by-one" THEN s.q.flips > s.q.threshold
                        ELSE s.q.flips >= s.q.threshold) THEN "clean-needed"
                   ELSE "ok"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == NormalizeRequest \/ Validate \/ Translate \/ Dispatch \/ Account \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
PartitionBounds == s.admitted => Within(s.q)
Translation == s.dispatched => s.address = s.q.off + OffsetSum(Len(Offsets))
RejectedLengths == s.pc = "done" /\ ~s.admitted => s.retlen = 0 /\ s.oobretlen = 0
ReadOnly == s.dispatched /\ s.q.write => s.q.writable
ReturnedLengths == s.pc = "done" /\ s.admitted =>
    s.retlen = s.q.dataret /\ s.oobretlen = s.q.oobret /\
    s.retlen <= s.q.len /\ s.oobretlen <= s.q.ooblen
EccStats == s.pc = "done" /\ s.admitted =>
    \A i \in 1..Len(Offsets) :
        s.ancestors[i] = s.corrected /\ s.failedAncestors[i] = s.failed
RequestStats == s.pc = "done" =>
    IF s.admitted /\ s.q.stats /\ ~s.q.write THEN
        /\ s.reqCorrected = s.q.corrected /\ s.reqFailed = s.q.failed
        /\ s.maxflips = IF s.q.ecc /\ s.q.error = "none" THEN s.q.flips ELSE 0
    ELSE s.maxflips = 99 /\ s.reqCorrected = 99 /\ s.reqFailed = 99
Result == s.pc = "done" /\ s.admitted =>
    s.result = IF s.q.error # "none" THEN s.q.error
               ELSE IF ~s.q.write /\ s.q.ecc /\ s.q.flips >= s.q.threshold
                    THEN "clean-needed" ELSE "ok"
Terminates == <> (s.pc = "done")
=============================================================================
