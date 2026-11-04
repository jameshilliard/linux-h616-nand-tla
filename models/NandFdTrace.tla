---------------------------- MODULE NandFdTrace ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* The observed current-source behavior includes retained publication and
\* close-time nand_sync on already released driver resources. Integrity
\* checks are separate from the deliberately failing lifetime property.
EXTENDS Integers, Sequences
CONSTANT Traces
VARIABLE s
vars == <<s>>
Init == \E t \in 1..Len(Traces): s=[t |-> t,i |-> 1,refs |-> 1,
    users |-> 0,registration |-> TRUE,resources |-> TRUE,published |-> TRUE,
    deadLocks |-> 0,reopen |-> FALSE,valid |-> TRUE,observations |-> TRUE]
T == Traces[s.t]
Advance == /\ s.i <= Len(T.events)
    /\ LET e == T.events[s.i]
           k == e[1]
           opens == k = "open" \/ (k = "reopen" /\ s.published)
           refs == IF opens THEN s.refs+1 ELSE IF k \in {"close","cleanup"} THEN s.refs-1 ELSE s.refs
           users == IF opens THEN s.users+1 ELSE IF k = "close" THEN s.users-1 ELSE s.users
           resources == s.resources /\ k # "cleanup"
           published == s.published /\ refs # 0
           deadLocks == s.deadLocks + IF k = "close" /\ T.write = 1 /\ ~s.resources /\ ~s.reopen THEN 2 ELSE 0
           ret == IF k = "reopen" /\ ~s.published THEN -19 ELSE 0
       IN s'=[s EXCEPT !.i=@+1,!.refs=refs,!.users=users,!.resources=resources,
           !.published=published,!.registration=IF k="cleanup" THEN FALSE ELSE @,
           !.reopen=IF k="reopen" /\ opens THEN TRUE ELSE IF k="close" THEN FALSE ELSE @,
           !.deadLocks=deadLocks,
           !.valid=@ /\ k \in {"init","open","close","cleanup","reopen","finish"} /\
               (k="close" => s.users>0) /\ (k="cleanup" => s.registration),
           !.observations=@ /\ e[2]=ret /\ e[3]=refs /\ e[4]=users /\
               (e[5]=1 <=> published) /\ (e[6]=1 <=> resources) /\
               e[7]=deadLocks /\ (e[8]=1 <=> published)]
Done == s.i>Len(T.events) /\ UNCHANGED vars
Next == Advance \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Protocol == s.valid
Snapshots == s.observations
ReferenceBalance == s.refs=s.users+(IF s.registration THEN 1 ELSE 0)
Released == s.i>Len(T.events) => s.refs=0 /\ s.users=0 /\ ~s.published /\ ~s.resources
NoLockAfterCleanup == s.deadLocks=0
Terminates == <> (s.i>Len(T.events))
=============================================================================
