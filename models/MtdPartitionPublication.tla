---------------------- MODULE MtdPartitionPublication ----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* A bounded registration/BLKPG deletion race. Mutex and handle-reference
\* contracts are abstract; this is not a C refinement or weak-memory proof.
EXTENDS Naturals
CONSTANTS EarlyPublish, HoldParseRef, RejectRemovedParent
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "allocate", lock |-> FALSE, listed |-> FALSE,
    device |-> FALSE, alive |-> TRUE, registered |-> FALSE, ref |-> FALSE,
    deleted |-> FALSE, badDevice |-> FALSE, badData |-> FALSE, badChild |-> FALSE]
Publish == /\ s.pc = "allocate"
    /\ s' = [s EXCEPT !.pc = "register", !.listed = TRUE,
                      !.lock = ~EarlyPublish]
Register == /\ s.pc = "register"
    /\ s' = [s EXCEPT !.pc = "parse", !.device = TRUE, !.registered = TRUE,
                      !.ref = HoldParseRef, !.lock = FALSE,
                      !.badData = @ \/ ~s.alive]
Delete == /\ s.listed /\ ~s.lock /\ ~s.deleted
    /\ s' = [s EXCEPT !.listed = FALSE, !.registered = FALSE, !.deleted = TRUE,
                      !.badDevice = @ \/ ~s.device,
                      !.alive = IF s.ref THEN @ ELSE FALSE]
Parse == /\ s.pc = "parse"
    /\ s' = [s EXCEPT !.pc = "put", !.badData = @ \/ ~s.alive,
                      !.badChild = @ \/ (~RejectRemovedParent /\ ~s.registered)]
Put == /\ s.pc = "put"
    /\ s' = [s EXCEPT !.pc = "done", !.ref = FALSE,
                      !.alive = IF s.deleted THEN FALSE ELSE @]
Finished == s.pc = "done" /\ s.deleted
Done == Finished /\ UNCHANGED vars
Next == Publish \/ Register \/ Delete \/ Parse \/ Put \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ClassDevicePublished == ~s.badDevice
ParserStorage == ~s.badData
NoOrphanChild == ~s.badChild
Terminates == <>Finished
=============================================================================
