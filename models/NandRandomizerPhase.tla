------------------------ MODULE NandRandomizerPhase ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Advance a BSP page seed through a full data step and the 15-bit offset.
\* Tables/observed software transforms are constants from the source runner.
EXTENDS NandRandomizerMath
CONSTANTS Inputs, StepBytes, Mutation
VARIABLE s
vars == <<s>>
Warmup == CASE Mutation = "phase-minus-one" -> 14
           [] Mutation = "phase-plus-one" -> 16
           [] OTHER -> 15
Init == \E i \in 1..Len(Inputs):
    s = [index |-> i, pos |-> 0,
         word |-> Advance(Inputs[i].pageSeed, Warmup)]
Byte == /\ s.pos < StepBytes
    /\ s' = [s EXCEPT !.pos = @ + 1, !.word = Advance(@, 8)]
Done == s.pos = StepBytes /\ UNCHANGED vars
Next == Byte \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
SeedTable == s.pos = StepBytes => s.word = Inputs[s.index].eccSeed
ByteStream == s.pos = StepBytes => Mask(s.word, 4) = Inputs[s.index].bytes
Bounds == s.word \in 0..32767 /\ s.pos \in 0..StepBytes
Terminates == <> (s.pos = StepBytes)
=============================================================================
