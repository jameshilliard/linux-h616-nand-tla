------------------------- MODULE NandNegotiation -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* nand_reset_interface/nand_setup_interface order and return semantics.
\* Initial mode is bootloader-owned. Device feature behavior is a contract.
EXTENDS Integers
CONSTANTS BootMode, Desired, Features, AckMatches, Fault, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "host-reset", host |-> BootMode, chip |-> BootMode,
    cached |-> BootMode, rc |-> "unset", resetFailed |-> FALSE]
HostReset ==
    /\ s.pc = "host-reset"
    /\ s' = [s EXCEPT !.host = 0, !.cached = 0, !.pc = "chip-reset"]
ChipReset ==
    /\ s.pc = "chip-reset"
    /\ s' = [s EXCEPT !.chip = 0, !.pc = "set-feature"]
SetFeature ==
    /\ s.pc = "set-feature"
    /\ \E applied \in (IF Features = "none" THEN {0}
                       ELSE IF AckMatches THEN {Desired} ELSE {0, Desired}) :
        s' = IF Fault = "set" /\ Features # "none" THEN
            [s EXCEPT !.rc = "error", !.pc = "done"] ELSE
            [s EXCEPT !.chip = applied, !.pc = "host-setup"]
HostSetup ==
    /\ s.pc = "host-setup"
    /\ s' = IF Fault = "host" THEN [s EXCEPT !.rc = "error", !.pc = "done"] ELSE
        [s EXCEPT !.host = Desired, !.pc = "read-ack"]
ReadAck ==
    /\ s.pc = "read-ack"
    /\ s' = IF Features # "get" \/ (AckMatches /\ Fault # "get") THEN
        [s EXCEPT !.cached = Desired, !.rc = "ok", !.pc = "done"] ELSE
        [s EXCEPT !.rc = IF Fault = "get" THEN "error" ELSE "ok", !.pc = "recover-host"]
RecoverHost ==
    /\ s.pc = "recover-host"
    /\ s' = [s EXCEPT !.host = IF Fault = "recover-host" THEN @ ELSE 0,
        !.cached = 0, !.resetFailed = Fault = "recover-host", !.pc = "recover-chip"]
RecoverChip ==
    /\ s.pc = "recover-chip"
    /\ s' = [s EXCEPT !.chip = IF Fault = "recover-chip" THEN @ ELSE 0,
        !.resetFailed = @ \/ Fault = "recover-chip", !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == HostReset \/ ChipReset \/ SetFeature \/ HostSetup \/ ReadAck \/ RecoverHost \/ RecoverChip \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Bounds == s.host \in 0..5 /\ s.chip \in 0..5 /\ s.cached \in 0..5
\* A chip lacking SET_FEATURES can accept faster SDR cycles without changing
\* a timing-mode latch; comparing numeric latch values would be a false alarm.
ModeAgreement == s.pc = "done" /\ s.rc = "ok" /\ Features # "none" =>
    s.host = s.cached /\ s.host = s.chip
RecoveryError == s.pc = "done" /\ s.resetFailed => s.rc # "ok"
Terminates == <> (s.pc = "done")
=============================================================================
