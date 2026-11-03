------------------------------ MODULE NandCore ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* NAND-core contract layer: format admission, whole-page vendor classification,
\* statistics delta semantics, one read-retry, decoded BBT/BBM preservation.
EXTENDS Integers, Sequences
CONSTANTS Vendor, Engine, Scramble, SmallPage, Retry, Initial, CounterMod, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "attach", engine |-> Engine, flags |-> {}, accepted |-> FALSE,
    attempt |-> 0, corrected |-> Initial, failed |-> Initial,
    result |-> "unset", kind |-> "unset", full |-> FALSE, bbm |-> 165,
    expectedCorrected |-> Initial, expectedFailed |-> Initial]
Attach ==
    /\ s.pc = "attach"
    /\ LET allowed == IF Vendor THEN Engine = "host" ELSE Engine \in {"host","soft","none"}
       IN s' = [s EXCEPT !.accepted = allowed,
            !.pc = IF allowed THEN "core-tail" ELSE "done",
            !.flags = (IF Vendor \/ Scramble THEN {"scramble","no-subwrite"} ELSE {})
                \cup {"subread","bbt-no-oob"}]
CoreTail ==
    /\ s.pc = "core-tail"
    /\ s' = [s EXCEPT !.engine = IF SmallPage /\ Engine = "host" THEN "soft" ELSE @,
        !.pc = "read"]
Read ==
    /\ s.pc = "read"
    /\ \E k \in {"clean", "corrected", "uncorrectable", "transport"} \cup
                (IF Vendor THEN {"zero"} ELSE {}) :
        s' = [s EXCEPT !.kind = k, !.attempt = @ + 1, !.pc = "interpret",
            !.full = Vendor /\ Mutation # "partial-vendor",
            !.corrected = (Initial + IF k = "corrected" THEN 3 ELSE 0) % CounterMod,
            !.failed = (Initial + IF k \in {"uncorrectable","zero"} THEN 1 ELSE 0) % CounterMod,
            !.expectedCorrected = (Initial + IF k = "corrected" THEN 3 ELSE 0) % CounterMod,
            !.expectedFailed = (Initial + IF k \in {"uncorrectable","zero"} THEN 1 ELSE 0) % CounterMod,
            !.bbm = IF k = "zero" THEN 0
                    ELSE IF Mutation = "erase-bbt-on-error" /\ k = "uncorrectable" THEN 255 ELSE 165]
Interpret ==
    /\ s.pc = "interpret"
    /\ s' = [s EXCEPT !.result =
        IF s.kind = "transport" THEN "io"
        ELSE IF s.failed # Initial /\ Mutation # "ignore-failed-delta" THEN "badmsg"
        ELSE IF s.kind = "corrected" THEN "corrected" ELSE "ok",
        !.pc = "decision"]
ReadRetry ==
    /\ s.pc = "decision" /\ Retry /\ s.result = "badmsg" /\ s.attempt = 1
    /\ s' = [s EXCEPT !.pc = "retry-reset",
        !.failed = IF Mutation = "leak-retry-stats" THEN @ ELSE Initial,
        !.corrected = IF Mutation = "leak-retry-stats" THEN @ ELSE Initial]
RetryReset ==
    /\ s.pc = "retry-reset"
    /\ s' = [s EXCEPT !.pc = "read"]
Return ==
    /\ s.pc = "decision"
    /\ ~Retry \/ s.result # "badmsg" \/ s.attempt = 2
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Attach \/ CoreTail \/ Read \/ Interpret \/ ReadRetry \/ RetryReset \/ Return \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
FormatAdmission == s.accepted /\ Vendor => Engine = "host" /\
    {"scramble","no-subwrite"} \subseteq s.flags
WholePageClassification == Vendor /\ s.attempt > 0 => s.full
ErrorSemantics == s.pc = "decision" => s.result =
    IF s.kind = "transport" THEN "io"
    ELSE IF s.kind \in {"uncorrectable","zero"} THEN "badmsg"
    ELSE IF s.kind = "corrected" THEN "corrected" ELSE "ok"
RetryAccounting == s.pc = "retry-reset" => s.failed = Initial /\ s.corrected = Initial
DecodedMetadata == s.bbm = IF s.kind = "zero" THEN 0 ELSE 165
FinalAccounting == s.pc = "decision" =>
    s.corrected = s.expectedCorrected /\ s.failed = s.expectedFailed
Terminates == <> (s.pc = "done")
=============================================================================
