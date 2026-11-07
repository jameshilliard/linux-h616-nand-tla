-------------------------- MODULE MtdOtpLifetime --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* User/factory OTP consumers and bare NVMEM device references. A provider
\* context pin is independent of the MTD device's own registration pin.
\* MtdRegistered=FALSE covers registration unwind after OTP publication;
\* one provider corresponds to failure while discovering factory OTP.
EXTENDS Integers, FiniteSets
CONSTANTS Providers, MtdRegistered, RetainContext, ReleaseEarly, ClearPointers
P == 1..Providers
VARIABLE s
vars == <<s>>

Init == s = [initial |-> TRUE, mtd |-> MtdRegistered, alive |-> TRUE,
             registered |-> P, pointers |-> P, consumers |-> P,
             deviceRefs |-> P, providers |-> P,
             pins |-> IF RetainContext THEN P ELSE {},
             phase |-> 0, doublePut |-> FALSE]

Unregister == /\ s.phase = 0
    /\ s' = [s EXCEPT !.registered = {}, !.phase = 1,
                      !.pointers = IF ClearPointers THEN {} ELSE @,
                      !.pins = IF ReleaseEarly THEN {} ELSE @]

OuterUnwind == /\ ~MtdRegistered /\ s.phase = 1
    /\ s' = [s EXCEPT !.phase = 2,
                      !.doublePut = s.pointers # {}]

DropMtd == /\ s.phase >= 1 /\ s.mtd
    /\ s' = [s EXCEPT !.mtd = FALSE]

DropInitial == /\ s.phase >= 1 /\ ~s.mtd /\ s.initial
    /\ (MtdRegistered \/ s.phase = 2)
    /\ s' = [s EXCEPT !.initial = FALSE]

ConsumerPut(p) == /\ p \in s.consumers
    /\ s' = [s EXCEPT !.consumers = @ \ {p}]

DevicePut(p) == /\ p \in s.deviceRefs
    /\ s' = [s EXCEPT !.deviceRefs = @ \ {p}]

ProviderRelease(p) == /\ p \in s.providers
    /\ p \notin (s.registered \cup s.consumers \cup s.deviceRefs)
    /\ s' = [s EXCEPT !.providers = @ \ {p}, !.pins = @ \ {p}]

BackingRelease == /\ s.alive /\ ~s.initial /\ ~s.mtd /\ s.pins = {}
    /\ s' = [s EXCEPT !.alive = FALSE]

Finish == ~s.alive /\ s.providers = {}
Done == Finish /\ UNCHANGED vars
Next == Unregister \/ OuterUnwind \/ DropMtd \/ DropInitial \/
        (\E p \in P: ConsumerPut(p) \/ DevicePut(p) \/ ProviderRelease(p)) \/
        BackingRelease \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
CallbackStorage == (s.consumers # {} \/ s.providers # {}) => s.alive
NoDoubleUnregister == ~s.doublePut
Terminates == <>Finish
=============================================================================
