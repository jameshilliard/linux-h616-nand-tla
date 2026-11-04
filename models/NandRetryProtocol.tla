------------------------ MODULE NandRetryProtocol ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Hynix enter/parameter/apply or Micron SET FEATURES, followed by the
\* core's retry read and restore. An error return is not an implicit reset.
EXTENDS Integers
CONSTANTS Vendor, FailPhase, FailStep, AcceptedOnError, Mutation
VARIABLE s
vars == <<s>>
N == IF Vendor = "hynix" THEN 4 ELSE 1
Last == IF Vendor = "hynix" THEN N+2 ELSE 1
Init == s = [pc |-> "configure", phase |-> 1, step |-> 1,
    staged |-> [i \in 1..N |-> 0], active |-> [i \in 1..N |-> 0],
    parameterMode |-> FALSE, error |-> FALSE, read |-> FALSE,
    interpretedDefault |-> FALSE, badRead |-> FALSE]
Failure == s.phase = FailPhase /\ s.step = FailStep
Accept == ~Failure \/ AcceptedOnError
Params == [i \in 1..N |-> s.phase]
Step == /\ s.pc = "configure"
    /\ s' = [s EXCEPT
        !.parameterMode = IF Vendor = "hynix" /\ Accept
            THEN IF s.step = 1 THEN TRUE ELSE IF s.step = Last THEN FALSE ELSE @ ELSE @,
        !.staged = IF Accept /\ Vendor = "micron" THEN Params
            ELSE IF Accept /\ s.step \in 2..(N+1)
            THEN [@ EXCEPT ![s.step-1] = s.phase] ELSE @,
        !.active = IF Accept /\ s.step = Last
            THEN IF Vendor = "micron" THEN Params ELSE s.staged ELSE @,
        !.error = Failure,
        !.pc = IF Failure THEN "return" ELSE IF s.step = Last
            THEN IF s.phase = 1 THEN "read" ELSE "return" ELSE @,
        !.step = @ + 1]
Read == /\ s.pc = "read"
    /\ s' = [s EXCEPT !.read = TRUE,
        !.badRead = s.active # [i \in 1..N |-> 1] \/ s.parameterMode,
        !.phase = 0, !.step = 1, !.pc = "configure"]
Return == /\ s.pc = "return"
    /\ s' = [s EXCEPT !.pc = "done",
        !.interpretedDefault = Mutation = "assume-error-rolled-back" \/ ~s.error]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Step \/ Read \/ Return \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ReadUsesAppliedParameters == ~s.badRead
NoFalseDefault == s.pc = "done" /\ s.interpretedDefault =>
    s.active = [i \in 1..N |-> 0] /\ ~s.parameterMode
RestoreOnSuccess == s.pc = "done" /\ ~s.error =>
    s.active = [i \in 1..N |-> 0] /\ s.staged = [i \in 1..N |-> 0]
DefaultAtEveryReturn == s.pc = "done" =>
    s.active = [i \in 1..N |-> 0] /\ s.staged = [i \in 1..N |-> 0] /\ ~s.parameterMode
Terminates == <> (s.pc = "done")
=============================================================================
