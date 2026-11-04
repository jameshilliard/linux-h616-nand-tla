--------------------------- MODULE NandOwnership ---------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* DMA API ownership and CPU/device visibility; coherent descriptor publication
\* and streaming buffer ownership are distinct obligations.
EXTENDS Integers, FiniteSets
CONSTANTS Backend, Read, Coherent, AbortStops, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "cpu", mapped |-> FALSE, active |-> FALSE,
    cpuData |-> 0, deviceData |-> 0, descCpu |-> FALSE, descDevice |-> FALSE,
    consumed |-> FALSE, observed |-> -1, deviceObserved |-> -1,
    unsafe |-> FALSE, illegal |-> FALSE,
    submitted |-> FALSE, error |-> FALSE]
CpuPrepare ==
    /\ s.pc = "cpu"
    /\ s' = [s EXCEPT !.cpuData = IF Read THEN 0 ELSE 1, !.pc = "map"]
Map ==
    /\ s.pc = "map"
    /\ s' = [s EXCEPT !.mapped = TRUE,
        !.deviceData = IF Mutation = "skip-map-sync" THEN @ ELSE s.cpuData,
        !.pc = "descriptor"]
Descriptor ==
    /\ s.pc = "descriptor"
    /\ s' = [s EXCEPT !.descCpu = TRUE,
        !.descDevice = FALSE,
        !.pc = "barrier"]
PublishEarly ==
    /\ Coherent /\ s.descCpu /\ ~s.descDevice /\ s.pc \in {"barrier", "submit"}
    /\ s' = [s EXCEPT !.descDevice = TRUE]
Barrier ==
    /\ s.pc = "barrier"
    /\ s' = [s EXCEPT !.descDevice = IF Mutation = "missing-barrier" THEN @ ELSE s.descCpu,
                      !.pc = "submit"]
Submit ==
    /\ s.pc = "submit"
    /\ s' = [s EXCEPT !.active = TRUE, !.submitted = TRUE, !.pc = "wait",
        !.illegal = Backend = "external-legacy" /\ Mutation = "unconditional-mdma-mode"]
Device ==
    /\ s.pc = "wait" /\ s.active
    /\ s' = [s EXCEPT !.consumed = Backend # "descriptor" \/ s.descDevice,
        !.deviceData = IF Read THEN 1 ELSE @, !.deviceObserved = s.deviceData,
        !.active = FALSE, !.pc = "unmap"]
Timeout ==
    /\ s.pc = "wait"
    /\ s' = [s EXCEPT !.pc = "abort", !.error = TRUE]
Abort ==
    /\ s.pc = "abort"
    /\ s' = [s EXCEPT !.active = IF AbortStops THEN FALSE ELSE @, !.pc = "unmap"]
Unmap ==
    /\ s.pc = "unmap"
    /\ s' = [s EXCEPT !.unsafe = s.active, !.mapped = FALSE,
        !.cpuData = IF Read /\ Mutation # "skip-unmap-sync" THEN s.deviceData ELSE @,
        !.pc = "consume"]
Consume ==
    /\ s.pc = "consume"
    /\ s' = [s EXCEPT !.observed = s.cpuData, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == CpuPrepare \/ Map \/ Descriptor \/ PublishEarly \/ Barrier \/ Submit \/ Device \/
        Timeout \/ Abort \/ Unmap \/ Consume \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
DescriptorPublication == s.submitted /\ Backend = "descriptor" => s.descDevice
NoUnsafeUnmap == ~s.unsafe
DeviceVisibility == s.pc = "done" /\ ~s.error =>
    IF Read THEN s.observed = 1 ELSE s.deviceObserved = 1
RegisterOwnership == ~s.illegal
ReturnedOwnership == s.pc = "done" => ~s.mapped /\ ~s.active
Terminates == <> (s.pc = "done")
=============================================================================
