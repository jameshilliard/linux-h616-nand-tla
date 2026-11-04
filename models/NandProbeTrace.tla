----------------------------- MODULE NandProbeTrace ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Bounded trace conformance, not arbitrary C-to-TLA refinement. Traces contain
\* observations at the actual scan/list/MTD fixture boundaries. Model membership
\* is computed independently and compared with the observed post-call snapshots.
EXTENDS Integers, Sequences, FiniteSets
CONSTANTS Traces, Mutation
VARIABLE s
vars == <<s>>
Members(seq) == {seq[i] : i \in 1..Len(seq)}
Init == \E t \in 1..Len(Traces) :
    s = [t |-> t, pos |-> 1, active |-> 0, started |-> {}, scanned |-> {},
         linked |-> {}, listed |-> {}, published |-> {}, registered |-> {},
         removed |-> {}, result |-> "pending", failed |-> FALSE, done |-> FALSE,
         protocol |-> TRUE, available |-> TRUE, ordering |-> TRUE,
         cleanup |-> TRUE, snapshots |-> TRUE, ownership |-> TRUE,
         success |-> TRUE]
Input == Traces[s.t]
Available == Members(Input.available)
Events == Input.events
Advance ==
    /\ ~s.done /\ s.pos <= Len(Events)
    /\ LET e == Events[s.pos]
           id == e.chip
           kind == e.kind
           started == IF kind = "scan-start" THEN s.started \cup {id} ELSE s.started
           scanned == IF kind = "scan-ok" THEN s.scanned \cup {id} ELSE s.scanned
           listed == CASE kind = "link" -> s.listed \cup {id}
                       [] kind = "unlink" -> s.listed \ {id}
                       [] OTHER -> s.listed
           published == CASE kind = "register-ok" -> s.published \cup {id}
                          [] kind = "unregister" -> s.published \ {id}
                          [] OTHER -> s.published
           linked == IF kind = "link" THEN s.linked \cup {id} ELSE s.linked
           removed == IF kind = "unlink" THEN s.removed \cup {id} ELSE s.removed
           active == CASE kind = "scan-start" -> id
                       [] kind \in {"scan-ok", "scan-error"} -> 0
                       [] OTHER -> s.active
           valid == CASE kind = "scan-start" ->
                            s.active = 0 /\ id \notin s.started /\ ~s.failed /\ s.result = "pending"
                     [] kind \in {"scan-ok", "scan-error"} -> s.active = id /\ id # 0
                     [] kind = "link" -> id \in s.scanned /\ id \notin s.linked /\ ~s.failed
                     [] kind \in {"register-ok", "register-error"} ->
                            id \in s.listed /\ id \in s.scanned /\ id \notin s.registered
                            /\ ~s.failed /\ s.result = "pending"
                     [] kind = "unregister" -> id \in s.published
                     [] kind = "unlink" -> id \in s.listed /\ id \notin s.removed
                     [] kind = "probe-ok" -> s.result = "pending" /\ ~s.failed /\ s.active = 0
                     [] kind = "probe-error" -> s.result = "pending" /\ s.active = 0
                     [] kind = "finish" -> s.result # "pending" /\ s.pos = Len(Events)
                     [] OTHER -> FALSE
       IN s' = [s EXCEPT
            !.pos = @ + 1, !.started = started, !.scanned = scanned,
            !.listed = listed, !.published = published, !.active = active,
            !.linked = linked, !.removed = removed,
            !.registered = IF kind = "register-ok" THEN @ \cup {id} ELSE @,
            !.result = CASE kind = "probe-ok" -> "ok"
                         [] kind = "probe-error" -> "error" [] OTHER -> @,
            !.failed = @ \/ kind \in {"scan-error", "register-error", "probe-error"},
            !.done = s.pos = Len(Events),
            !.protocol = @ /\ valid /\ (s.pos < Len(Events) \/ kind = "finish"),
            !.available = @ /\ (id = 0 \/ id \in Available),
            !.ordering = @ /\
                (kind \in {"register-ok", "register-error"} =>
                    Available \subseteq s.scanned /\ s.active = 0) /\
                (kind = "scan-start" => s.published = {}),
            !.cleanup = @ /\ (kind = "unlink" => id \notin s.published),
            !.snapshots = @ /\ Members(e.listed) = listed /\ Members(e.published) = published,
            !.ownership = @ /\ (kind \in {"probe-error", "finish"} =>
                listed = {} /\ published = {} /\ scanned = removed),
            !.success = @ /\ (kind = "probe-ok" =>
                scanned = Available /\ listed = Available /\ published = Available)]
Done == s.done /\ UNCHANGED vars
Next == Advance \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Advance)
ProtocolValid == s.protocol
OnlyAvailableTouched == s.available
AllScannedBeforePublish == s.ordering
CleanupSafe == s.cleanup
SnapshotsMatchC == s.snapshots
FinalOwnership == s.ownership
SuccessComplete == s.success
Terminates == <> s.done
=============================================================================
