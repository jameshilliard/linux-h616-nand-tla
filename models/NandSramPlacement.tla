----------------------- MODULE NandSramPlacement -----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Symbolic data placement, NOT a reconstruction of the H616 RTL.
\* Observed dirty-state outcomes include a rotated full block and a short
\* command writing beyond its prefix. Unknown offsets/counts are contracts.
EXTENDS Integers, Sequences
CONSTANTS Count, Offset, Extra, Partial, Fault, Policy, RecoveryWorks, Mutation
VARIABLE s
vars == <<s>>
Bank == 8
Expected(req, i) == 100 * req + i
Init == s = [pc |-> "recover", request |-> 1, dirty |-> Fault # "none",
    ram |-> [i \in 1..(2 * Bank) |-> 0], before |-> [i \in 1..(2 * Bank) |-> 0],
    output |-> <<>>, published |-> FALSE, valid |-> FALSE,
    spill |-> FALSE, bad |-> FALSE, error |-> FALSE, transfer |-> 0]
Recover == /\ s.pc = "recover"
    /\ s' = [s EXCEPT
        !.pc = IF Policy = "reject" /\ s.dirty THEN "done" ELSE "position",
        !.dirty = IF Policy \in {"bus-reset", "discard"} /\ RecoveryWorks
                 THEN FALSE ELSE @,
        !.ram = IF Policy = "clear-sram" THEN [i \in 1..(2 * Bank) |-> 0] ELSE @]
Position == /\ s.pc = "position"
    /\ s' = [s EXCEPT !.before = s.ram, !.published = FALSE,
        !.error = Partial /\ s.request = 1, !.transfer = 0, !.pc = "transfer"]
Transfer == /\ s.pc = "transfer"
    /\ LET broken == s.dirty /\ s.request = 1
           n == IF s.error THEN Count \div 2 ELSE Count
           moved == IF broken /\ Fault = "spill" THEN n + Extra ELSE n
           addresses == {IF broken THEN ((j - 1 + Offset) % Bank) + 1 ELSE j:
                         j \in 1..moved}
           ram == [i \in 1..(2 * Bank) |->
               IF i \in addresses THEN
                   Expected(s.request, IF broken THEN ((i - 1 - Offset + Bank) % Bank) + 1 ELSE i)
               ELSE s.ram[i]]
       IN s' = [s EXCEPT !.ram = ram, !.transfer = moved,
           !.spill = @ \/ (\E i \in (Count + 1)..Bank: ram[i] # s.before[i]),
           !.pc = "complete"]
Complete == /\ s.pc = "complete"
    /\ s' = [s EXCEPT !.dirty = FALSE, !.pc = "copy"]
Copy == /\ s.pc = "copy"
    /\ s' = IF s.error /\ Mutation # "copy-failed-prefix" THEN
        [s EXCEPT !.pc = "next"]
        ELSE [s EXCEPT !.published = TRUE,
            !.output = [i \in 1..Count |-> s.ram[i]],
            !.valid = ~s.error,
            !.bad = @ \/ s.error \/ (\E i \in 1..Count: s.ram[i] # Expected(s.request, i)),
            !.pc = "next"]
NextRequest == /\ s.pc = "next"
    /\ s' = IF s.request = 1 THEN [s EXCEPT !.request = 2,
            !.published = FALSE, !.pc = "position"]
        ELSE [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Recover \/ Position \/ Transfer \/ Complete \/ Copy \/ NextRequest \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
SoundCopy == ~s.bad
SecondBankPreserved == \A i \in (Bank + 1)..(2 * Bank): s.ram[i] = 0
CleanPrefix == Fault = "none" => ~s.spill
FailedReadNotPublished == s.error => ~s.published
SecondReadFresh == s.request = 2 /\ s.published =>
    s.output = [i \in 1..Count |-> Expected(2, i)]
Terminates == <> (s.pc = "done")
=============================================================================
