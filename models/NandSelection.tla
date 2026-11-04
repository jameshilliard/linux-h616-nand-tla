---------------------------- MODULE NandSelection ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Serialized chip selection: a rate-cache hit still updates chip-specific
\* registers. Clock failure must stop both exec_op and direct DMA writes.
\* Rates are abstract profiles, not a proof of CCF rounding or electrical
\* timing. ErrorKeepsClock states an external provider failure contract.
EXTENDS Integers, Sequences
CONSTANTS Steps, Rates, Callers, ErrorKeepsClock, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "begin", n |-> 0, chip |-> 1, caller |-> "exec",
    fail |-> FALSE, error |-> FALSE, need |-> TRUE,
    cache |-> 0, clock |-> 0, context |-> 0,
    old |-> [cache |-> 0, clock |-> 0, context |-> 0, attempts |-> 0, transfers |-> 0],
    attempts |-> 0, transfers |-> 0, wrong |-> FALSE]

Begin ==
    /\ s.pc = "begin" /\ s.n < Steps
    /\ \E chip \in 1..Len(Rates), caller \in Callers, fail \in BOOLEAN :
        s' = [s EXCEPT !.chip = chip, !.caller = caller, !.fail = fail,
            !.error = FALSE, !.need = s.cache # Rates[chip],
            !.old = [cache |-> s.cache, clock |-> s.clock, context |-> s.context,
                     attempts |-> s.attempts, transfers |-> s.transfers],
            !.pc = IF caller = "check" /\ Mutation # "program-check-only"
                   THEN "return" ELSE "clock"]
Clock ==
    /\ s.pc = "clock" /\ s.need
    /\ s' = IF s.fail THEN
        [s EXCEPT !.attempts = @ + 1, !.error = TRUE,
            !.clock = IF ErrorKeepsClock THEN @ ELSE -1,
            !.cache = IF Mutation = "cache-failed-rate" THEN Rates[s.chip] ELSE @,
            !.pc = IF Mutation = "ignore-error-" \o s.caller THEN "program" ELSE "return"]
        ELSE [s EXCEPT !.attempts = @ + 1, !.cache = Rates[s.chip],
            !.clock = Rates[s.chip], !.pc = "program"]
Cached ==
    /\ s.pc = "clock" /\ ~s.need
    /\ s' = [s EXCEPT !.pc = IF Mutation = "skip-equal-context"
                            THEN "transfer" ELSE "program"]
Program ==
    /\ s.pc = "program"
    /\ s' = [s EXCEPT !.context = s.chip, !.pc = "transfer"]
Transfer ==
    /\ s.pc = "transfer"
    /\ s' = [s EXCEPT !.transfers = @ + 1,
        !.wrong = @ \/ s.error \/ s.caller = "check" \/
            s.context # s.chip \/ s.clock # Rates[s.chip], !.pc = "return"]
Return ==
    /\ s.pc = "return"
    /\ s' = [s EXCEPT !.n = @ + 1, !.pc = "begin"]
Finish ==
    /\ s.pc = "begin" /\ s.n = Steps
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Begin \/ Clock \/ Cached \/ Program \/ Transfer \/ Return \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
CacheMatchesClock == s.cache = s.clock
CheckOnlyPure == s.caller = "check" /\ s.pc \notin {"begin", "done"} =>
    s.cache = s.old.cache /\ s.clock = s.old.clock /\ s.context = s.old.context /\
    s.attempts = s.old.attempts /\ s.transfers = s.old.transfers
FailedSelectionPure == s.error /\ s.pc \notin {"begin", "done"} =>
    s.cache = s.old.cache /\ s.context = s.old.context
CorrectTransfer == ~s.wrong
BoundedProgress == s.transfers <= s.n + 1 /\ s.attempts <= s.n + 1
Terminates == <> (s.pc = "done")
=============================================================================
