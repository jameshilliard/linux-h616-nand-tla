-------------------------- MODULE NandProbePublication -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* nand_scan_ident() does not acquire controller->lock. Published chips can
\* already issue I/O under that mutex while the next child is being scanned.
\* This model isolates shared command/context interference, not unbind lifetime.
EXTENDS Integers, FiniteSets
CONSTANTS N, DeferredPublication, FailScan, FailRegister, Users, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "scan", i |-> 1, scanned |-> {}, published |-> {},
             selected |-> 0, user |-> 0, upc |-> "idle", used |-> FALSE,
             wrong |-> FALSE, result |-> "pending", done |-> FALSE]
ConfigureScan ==
    /\ s.pc = "scan" /\ s.i <= N
    /\ s' = [s EXCEPT !.selected = s.i, !.pc = "scan-command"]
ScanCommand ==
    /\ s.pc = "scan-command"
    /\ s' = [s EXCEPT !.wrong = @ \/ s.selected # s.i,
        !.pc = IF s.i = FailScan THEN "end"
               ELSE IF DeferredPublication THEN "next-scan" ELSE "register",
        !.scanned = IF s.i = FailScan THEN @ ELSE @ \cup {s.i},
        !.result = IF s.i = FailScan THEN "scan-error" ELSE @]
NextScan ==
    /\ s.pc = "next-scan"
    /\ s' = IF s.i < N THEN [s EXCEPT !.i = @ + 1, !.pc = "scan"]
        ELSE IF DeferredPublication THEN [s EXCEPT !.i = 1, !.pc = "register"]
        ELSE [s EXCEPT !.pc = "end", !.result = "ok"]
Register ==
    /\ s.pc = "register"
    /\ s' = IF s.i = FailRegister
        THEN [s EXCEPT !.pc = "end", !.result = "registration-error"]
        ELSE [s EXCEPT !.published = @ \cup {s.i},
            !.pc = IF ~DeferredPublication THEN "next-scan"
                   ELSE IF s.i = N THEN "end" ELSE "register",
            !.i = IF DeferredPublication /\ s.i < N THEN @ + 1 ELSE @,
            !.result = IF DeferredPublication /\ s.i = N THEN "ok" ELSE @]
UserBegin ==
    /\ Users /\ ~s.used /\ ~s.done /\ s.published # {}
    /\ \E chip \in s.published :
        s' = [s EXCEPT !.user = chip, !.upc = "select", !.used = TRUE]
UserSelect ==
    /\ s.upc = "select"
    /\ s' = [s EXCEPT !.selected = s.user, !.upc = "transfer"]
UserTransfer ==
    /\ s.upc = "transfer"
    /\ s' = [s EXCEPT !.wrong = @ \/ s.selected # s.user,
        !.upc = "idle", !.user = 0]
Finish ==
    /\ s.pc = "end" /\ s.upc = "idle" /\ ~s.done
    /\ s' = [s EXCEPT !.done = TRUE]
Done == s.done /\ UNCHANGED vars
Probe == ConfigureScan \/ ScanCommand \/ NextScan \/ Register
UserStep == UserSelect \/ UserTransfer
Next == Probe \/ UserBegin \/ UserStep \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Probe) /\ WF_vars(UserStep) /\ WF_vars(Finish)
NoContextInterference == ~s.wrong
PublishedScanned == s.published \subseteq s.scanned
UserOwnsPublishedChip == s.user # 0 => s.user \in s.published
Terminates == <> s.done
=============================================================================
