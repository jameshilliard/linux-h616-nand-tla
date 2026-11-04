----------------------- MODULE NandPostAbortRequest -----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Request-level composition of the measured post-abort data-path boundary.
\* This is an abstract fault model, NOT reconstructed controller RTL. A
\* rotated first block is an observation; the hidden cursor is a representation
\* of that observation, not identification of a particular silicon register.
\* StopRetires, DrainClears and BusClears remain explicit contracts. In
\* particular, a quiet retained buffer does not prove StopRetires.
EXTENDS Integers, Sequences
CONSTANTS DMA, Abort, Fault, Policy, StopRetires, DrainClears, BusClears,
          CompletionClears, Mutation
VARIABLE s
vars == <<s>>
Words == 4
Steps == 2
Expected(request, step) == [i \in 1..Words |-> request * 100 + step * 10 + i]
Rotate(data, offset) == [i \in 1..Words |-> data[((i - 1 + offset) % Words) + 1]]
Clean == [offset |-> 0, decodeError |-> FALSE]
Init == s = [pc |-> "reset", oldMapped |-> TRUE, oldProducer |-> Abort,
    quiet |-> FALSE, resetClear |-> FALSE, hidden |-> Clean,
    ram |-> <<0, 0, 0, 0>>, mapped |-> FALSE, request |-> 1, step |-> 1,
    cmdDone |-> FALSE, dmaDone |-> FALSE, eccGood |-> TRUE,
    captured |-> FALSE, output |-> <<>>, goodStatus |-> TRUE,
    published |-> FALSE, success |-> FALSE, fresh |-> TRUE, unsafe |-> FALSE]
Reset == /\ s.pc = "reset"
    /\ s' = [s EXCEPT !.pc = "observe", !.resetClear = TRUE,
        !.oldProducer = @ /\ ~StopRetires,
        !.hidden = IF Abort THEN
            [offset |-> IF Fault = "rotate" THEN 1 ELSE 0,
             decodeError |-> Fault = "decode"] ELSE Clean]
Observe == /\ s.pc = "observe"
    /\ s' = [s EXCEPT !.pc = "recover", !.quiet = TRUE,
        !.oldMapped = Mutation # "free-after-quiet",
        !.unsafe = Mutation = "free-after-quiet" /\ s.oldProducer]
Recover == /\ s.pc = "recover"
    /\ s' = [s EXCEPT
        !.pc = IF Abort /\ Policy = "quarantine" THEN "done" ELSE "map",
        !.hidden = IF (Policy = "drain" /\ DrainClears) \/
                        (Policy = "bus-reset" /\ BusClears)
                   THEN Clean ELSE @,
        !.ram = IF Policy = "clear-sram" THEN <<0, 0, 0, 0>> ELSE @]
Map == /\ s.pc = "map"
    /\ s' = [s EXCEPT !.pc = "position", !.mapped = DMA,
        !.output = <<>>, !.goodStatus = TRUE, !.published = FALSE]
Position == /\ s.pc = "position"
    \* A fresh NAND row/column and seed do not establish SRAM/data-path order.
    /\ s' = [s EXCEPT !.pc = "receive", !.cmdDone = FALSE,
        !.dmaDone = ~DMA, !.captured = FALSE]
Receive == /\ s.pc = "receive"
    /\ LET data == Expected(s.request, s.step)
           valid == ~s.hidden.decodeError
       IN s' = [s EXCEPT !.pc = "events",
           !.ram = IF valid THEN Rotate(data, s.hidden.offset)
                   ELSE <<-1, -1, -1, -1>>,
           !.eccGood = valid]
CommandDone == /\ s.pc = "events" /\ ~s.cmdDone
    /\ s' = [s EXCEPT !.cmdDone = TRUE,
        !.hidden = IF CompletionClears THEN Clean ELSE @]
DmaDone == /\ s.pc = "events" /\ DMA /\ ~s.dmaDone
    /\ s' = [s EXCEPT !.dmaDone = TRUE]
Capture == /\ s.pc = "events" /\ s.cmdDone /\ s.dmaDone
    /\ s' = [s EXCEPT !.pc = "copy", !.captured = TRUE,
        !.goodStatus = @ /\ s.eccGood]
Copy == /\ s.pc = "copy" /\ s.captured
    /\ s' = [s EXCEPT !.output = Append(@, s.ram), !.pc = "advance"]
Advance == /\ s.pc = "advance"
    /\ s' = IF s.step < Steps
        THEN [s EXCEPT !.step = @ + 1, !.pc = "position"]
        ELSE [s EXCEPT !.pc = "return"]
Return == /\ s.pc = "return"
    /\ s' = [s EXCEPT !.mapped = FALSE, !.published = TRUE,
        !.success = s.goodStatus,
        !.fresh = s.output = [i \in 1..Steps |-> Expected(s.request, i)],
        !.pc = "next"]
NextRequest == /\ s.pc = "next"
    /\ s' = IF s.request = 1 THEN
        [s EXCEPT !.request = 2, !.step = 1, !.published = FALSE, !.pc = "map"]
        ELSE [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Reset \/ Observe \/ Recover \/ Map \/ Position \/ Receive \/
        CommandDone \/ DmaDone \/ Capture \/ Copy \/ Advance \/ Return \/
        NextRequest \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Geometry == /\ Len(s.ram) = Words /\ Len(s.output) <= Steps
            /\ s.step \in 1..Steps /\ s.request \in 1..2
MappingSafe == ~s.unsafe
ProbeOwnership == s.oldMapped
CompleteBeforeCopy == s.captured => s.cmdDone /\ s.dmaDone
Accounted == s.published => Len(s.output) = Steps /\ ~s.mapped
SoundSuccess == s.published /\ s.success => s.fresh
SecondRequestFresh == CompletionClears /\ s.request = 2 /\ s.published => s.fresh
Terminates == <> (s.pc = "done")
=============================================================================
