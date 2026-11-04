-------------------------- MODULE NandMtdLifetime --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* MTD kref release is distinct from driver/devres ownership. The source
\* unregister path drops a reference; it does not wait for all open users.
EXTENDS Integers
CONSTANTS Users, ExcludeUnbindWithUsers, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "open", refs |-> 1, opened |-> 0, active |-> FALSE,
    registered |-> TRUE, resources |-> TRUE, released |-> FALSE, unsafe |-> FALSE]
Open == /\ s.pc = "open" /\ s.opened < Users
    /\ s' = [s EXCEPT !.refs = @ + 1, !.opened = @ + 1]
Begin == /\ s.pc = "open" /\ s.opened = Users
    /\ s' = [s EXCEPT !.active = Users > 0, !.pc = "remove"]
FinishIO == /\ s.active
    /\ s' = [s EXCEPT !.unsafe = @ \/ ~s.resources, !.active = FALSE]
Close == /\ s.pc # "open" /\ s.opened > 0 /\ ~s.active
    /\ s' = [s EXCEPT !.opened = @ - 1, !.refs = @ - 1,
        !.released = s.refs = 1]
Unregister == /\ s.pc = "remove"
    /\ (~ExcludeUnbindWithUsers \/ s.opened = 0)
    /\ s' = [s EXCEPT !.refs = @ - 1, !.released = s.refs = 1,
        !.registered = FALSE, !.pc = "cleanup"]
Cleanup == /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.resources = FALSE, !.pc = "close",
        !.unsafe = @ \/ s.active \/ (Mutation = "early-free" /\ s.opened > 0)]
Finish == /\ s.pc = "close" /\ s.opened = 0
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Open \/ Begin \/ FinishIO \/ Close \/ Unregister \/ Cleanup \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next) /\ WF_vars(FinishIO) /\ WF_vars(Close)
ReferenceBalance == s.refs = s.opened + (IF s.registered THEN 1 ELSE 0)
ReleaseAtZero == s.released <=> s.refs = 0
ResourcesWhileActive == ~s.unsafe
Terminates == <> (s.pc = "done")
=============================================================================
