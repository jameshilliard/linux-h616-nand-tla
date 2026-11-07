------------------------- MODULE MtdRetainedNumber -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* One old MTD, a retained NVMEM consumer, and a replacement registration.
\* IDA reserves the number through final device release; IDR lookup visibility
\* ends at the last MTD put. NVMEM device_del precedes the parent device put.
EXTENDS Integers
CONSTANT HoldNumber
VARIABLE s
vars == <<s>>
Init == s = [visible |-> TRUE, consumer |-> TRUE, provider |-> TRUE,
             device |-> TRUE, reserved |-> TRUE, replacement |-> -1,
             collision |-> FALSE]
LastMtdPut == /\ s.visible
    /\ s' = [s EXCEPT !.visible = FALSE,
                      !.reserved = IF HoldNumber THEN @ ELSE FALSE]
ConsumerPut == /\ s.consumer
    /\ s' = [s EXCEPT !.consumer = FALSE]
ProviderRelease == /\ ~s.visible /\ ~s.consumer /\ s.provider
    /\ s' = [s EXCEPT !.provider = FALSE]
DeviceRelease == /\ ~s.visible /\ ~s.provider /\ s.device
    /\ s' = [s EXCEPT !.device = FALSE, !.reserved = FALSE]
RegisterReplacement == /\ ~s.visible /\ s.replacement = -1
    /\ s' = [s EXCEPT !.replacement = IF s.reserved THEN 1 ELSE 0,
                      !.collision = s.provider /\ ~s.reserved]
Finish == ~s.device /\ s.replacement # -1
Done == Finish /\ UNCHANGED vars
Next == LastMtdPut \/ ConsumerPut \/ ProviderRelease \/ DeviceRelease \/
        RegisterReplacement \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
NoNameCollision == ~s.collision
Terminates == <>Finish
=============================================================================
