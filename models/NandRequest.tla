--------------------------- MODULE NandRequest ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* nand_do_read_ops order: callback, data/OOB copy, ECC retry, page accounting.
\* Page IDs stand for distinct payloads. No invented atomic OOB commit on retry.
EXTENDS Integers, Sequences
CONSTANTS Pages, Oob, Retry, FailPage, TransportPage, Mode, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "read", page |-> 1, attempt |-> 0, failed |-> 0,
    saved |-> 0, data |-> <<>>, oob |-> <<>>, retlen |-> 0, result |-> "unset",
    bad |-> FALSE, calls |-> 0]
Read ==
    /\ s.pc = "read"
    /\ s' = [s EXCEPT !.pc = IF s.page = TransportPage THEN "error" ELSE "copy",
        !.failed = IF s.page = FailPage /\ s.attempt = 0 /\ Mode # "raw"
                       THEN @ + 1 ELSE @, !.calls = @ + 1]
Copy ==
    /\ s.pc = "copy"
    /\ s' = [s EXCEPT !.data = IF Len(@) < s.page THEN Append(@, s.page)
                                                        ELSE @,
        !.oob = IF Oob /\ Len(@) < Pages THEN Append(@, s.page) ELSE @,
        !.pc = "interpret"]
Interpret ==
    /\ s.pc = "interpret"
    /\ s' = IF s.failed > s.saved /\ Retry /\ s.attempt = 0 THEN
        [s EXCEPT !.pc = "read", !.attempt = 1,
            !.failed = IF Mutation = "leak-retry-stats" THEN @ ELSE s.saved]
       ELSE [s EXCEPT !.pc = "advance",
                      !.bad = @ \/ s.failed > s.saved]
Advance ==
    /\ s.pc = "advance"
    /\ s' = [s EXCEPT !.retlen = @ + 1, !.page = @ + 1,
        !.attempt = 0, !.saved = s.failed,
        !.pc = IF s.page = Pages THEN "finish" ELSE "read"]
Finish ==
    /\ s.pc = "finish"
    /\ s' = [s EXCEPT !.result = IF s.bad THEN "ecc" ELSE "ok", !.pc = "done"]
Error ==
    /\ s.pc = "error"
    /\ s' = [s EXCEPT !.result = "io", !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Read \/ Copy \/ Interpret \/ Advance \/ Finish \/ Error \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Lengths == s.retlen <= Pages /\ Len(s.oob) <= Pages /\ s.retlen <= Len(s.data)
DataIdentity == \A i \in 1..Len(s.data) : s.data[i] = i
OobIdentity == s.result = "ok" /\ Oob => s.oob = [i \in 1..Pages |-> i]
\* This finite retry scenario guarantees the second attempt succeeds. Check
\* the result too, so leaked statistics cannot hide behind a false ECC error.
RetryStats == (s.result = "ok" => s.failed = 0) /\
    (Retry /\ s.result \in {"ok", "ecc"} => s.failed = 0 /\ s.result = "ok")
ErrorPrefix == s.result = "io" => s.retlen = TransportPage - 1
Terminates == <> (s.pc = "done")
=============================================================================
