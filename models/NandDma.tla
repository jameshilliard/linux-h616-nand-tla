------------------------------ MODULE NandDma ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Actual three backends; descriptor visibility is separate from CPU stores.
EXTENDS Integers, Sequences
CONSTANTS Backend, Length, Address, Chunks, MapOK, PrepOK, SubmitOK, Mutation
VARIABLE s
vars == <<s>>
LE32(n) == <<n % 256, (n \div 256) % 256, (n \div 65536) % 256,
             (n \div 16777216) % 256>>
Expected == <<LE32(12), LE32(Length), Address, LE32(4096)>>
Valid == Backend # "descriptor" \/
    (Length <= 65535 /\ Length % 8 = 0 /\ Address[1] % 4 = 0)
CountBits == {b \in 0..5 : (Chunks \div 2^b) % 2 = 1}
Init == s = [pc |-> "map", mapped |-> FALSE, active |-> FALSE, ram |-> FALSE,
    maps |-> 0, unmaps |-> 0, cpu |-> <<>>, visible |-> <<>>, slot |-> 1,
    published |-> FALSE, fetched |-> FALSE, observed |-> <<>>, blocks |-> {},
    result |-> "unset", invalid |-> FALSE, unsafe |-> FALSE]
Map ==
    /\ s.pc = "map"
    /\ s' = IF MapOK THEN [s EXCEPT !.pc = "validate", !.mapped = TRUE, !.maps = 1]
            ELSE [s EXCEPT !.pc = "done", !.result = "map-error"]
Validate ==
    /\ s.pc = "validate"
    /\ s' = IF Valid \/ Mutation = "no-validation" THEN [s EXCEPT !.pc = "prepare"]
            ELSE [s EXCEPT !.pc = "unmap", !.result = "invalid"]
Prepare ==
    /\ s.pc = "prepare"
    /\ s' = IF Backend = "external" /\ ~PrepOK THEN
        [s EXCEPT !.pc = "unmap", !.result = "prepare-error"] ELSE
        [s EXCEPT !.pc = IF Backend = "descriptor" THEN "store" ELSE "submit",
            !.ram = TRUE, !.blocks = IF Backend = "descriptor" /\
                Mutation # "count-not-mask" THEN 0..(Chunks-1) ELSE CountBits]
Store ==
    /\ s.pc = "store"
    /\ s' = [s EXCEPT !.cpu = Append(@, Expected[s.slot]), !.slot = @ + 1,
        !.pc = IF s.slot = 4 THEN "barrier" ELSE "store"]
Propagate ==
    /\ Len(s.visible) < Len(s.cpu) /\ ~s.fetched
    /\ s' = [s EXCEPT !.visible = Append(@, s.cpu[Len(s.visible) + 1])]
Barrier ==
    /\ s.pc = "barrier"
    /\ s' = [s EXCEPT !.visible = IF Mutation = "no-barrier" THEN @ ELSE s.cpu,
                       !.pc = "submit"]
Submit ==
    /\ s.pc = "submit"
    /\ s' = IF Backend = "external" /\ ~SubmitOK THEN
        [s EXCEPT !.ram = Mutation = "leak-ram", !.pc = "unmap",
                   !.result = "submit-error"] ELSE
        [s EXCEPT !.published = TRUE, !.active = TRUE, !.pc = "fetch",
                   !.invalid = ~Valid]
Fetch ==
    /\ s.pc = "fetch"
    /\ s' = [s EXCEPT !.observed = IF Backend = "descriptor" THEN s.visible ELSE <<>>,
        !.fetched = TRUE, !.pc = "complete"]
Complete ==
    /\ s.pc = "complete"
    /\ s' = [s EXCEPT !.active = FALSE, !.result = "ok", !.pc = "unmap"]
Unmap ==
    /\ s.pc = "unmap"
    /\ s' = [s EXCEPT !.unsafe = @ \/ s.active, !.mapped = FALSE,
        !.unmaps = @ + 1, !.pc = "cleanup"]
Cleanup ==
    /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.ram = IF Mutation = "leak-ram" THEN @ ELSE FALSE, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Map \/ Validate \/ Prepare \/ Store \/ Propagate \/ Barrier \/ Submit \/ Fetch \/
        Complete \/ Unmap \/ Cleanup \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ValidSubmission == ~s.invalid
DescriptorVisible == Backend = "descriptor" /\ s.fetched => s.observed = Expected
BlockEncoding == s.published => s.blocks =
    IF Backend = "descriptor" THEN 0..(Chunks-1) ELSE CountBits
BalancedMaps == s.unmaps <= s.maps /\ (s.pc = "done" => s.maps = s.unmaps /\ ~s.mapped)
NoUnsafeUnmap == ~s.unsafe
CleanReturn == s.pc = "done" => ~s.ram /\ ~s.active
Terminates == <> (s.pc = "done")
=============================================================================
