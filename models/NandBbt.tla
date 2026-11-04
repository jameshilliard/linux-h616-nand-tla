------------------------------ MODULE NandBbt ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* BBM scan, RAM table, mirrored flash tables, runtime markbad and remount.
\* Marker visibility despite ECC error is an explicit decoded-OOB contract.
EXTENDS Integers, FiniteSets
CONSTANTS Blocks, FactoryBad, ScanError, ReadError, PreserveMarker, NoOobBbm, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "scan", block |-> 1, ram |-> {}, marker |-> FactoryBad,
    primary |-> {}, mirror |-> {}, known |-> FactoryBad, scanned |-> {},
    version |-> 0, pv |-> 0, mv |-> 0, result |-> "unset", scanFailed |-> FALSE]
Scan ==
    /\ s.pc = "scan" /\ s.block <= Blocks
    /\ s' = IF s.block = ScanError THEN
        \* check_create ignores create_bbt's negative return in pinned source.
        [s EXCEPT !.pc = "write-primary", !.scanFailed = TRUE, !.version = 1]
       ELSE [s EXCEPT !.ram = IF s.block \in s.marker /\ PreserveMarker
                               THEN @ \cup {s.block} ELSE @,
            !.scanned = @ \cup {s.block}, !.block = @ + 1]
ScanDone ==
    /\ s.pc = "scan" /\ s.block > Blocks
    /\ s' = [s EXCEPT !.pc = "write-primary", !.version = 1]
WritePrimary ==
    /\ s.pc = "write-primary"
    /\ s' = [s EXCEPT !.primary = s.ram, !.pv = s.version, !.pc = "write-mirror"]
WriteMirror ==
    /\ s.pc = "write-mirror"
    /\ s' = [s EXCEPT !.mirror = s.ram, !.mv = s.version,
        !.pc = IF s.version = 1 THEN "mark" ELSE "reboot"]
Mark ==
    /\ s.pc = "mark"
    /\ \E b \in (1..Blocks) \ s.known :
       s' = [s EXCEPT !.ram = @ \cup {b}, !.known = @ \cup {b},
          !.marker = IF NoOobBbm THEN @ ELSE @ \cup {b},
          !.version = 2, !.pc = "write-primary"]
Reboot ==
    /\ s.pc = "reboot"
    /\ s' = [s EXCEPT !.ram = {}, !.pc = "read"]
Read ==
    /\ s.pc = "read"
    /\ s' = [s EXCEPT !.ram = IF ReadError THEN {}
                            ELSE IF Mutation = "read-marker-only" THEN s.marker ELSE s.primary,
                      !.pc = "done", !.result = "ok"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Scan \/ ScanDone \/ WritePrimary \/ WriteMirror \/ Mark \/ Reboot \/ Read \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Bounds == s.ram \subseteq 1..Blocks /\ s.marker \subseteq 1..Blocks
NoFalseGood == s.result = "ok" => s.known \subseteq s.ram
MirrorsAgree == s.pc \in {"mark", "reboot", "read", "done"} =>
    s.primary = s.mirror /\ s.pv = s.mv
NoIncompletePublish == s.result = "ok" => ~s.scanFailed
Terminates == <> (s.pc = "done")
=============================================================================
