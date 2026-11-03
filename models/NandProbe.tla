------------------------------ MODULE NandProbe ------------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Manual versus devres ownership at each probe/scan/register failure boundary.
\* nand_scan() unwinds its own failure; successful scans need nand_cleanup().
EXTENDS Integers, FiniteSets, Sequences
CONSTANTS FailAt, Backend, MaskOK, AllocOK, Request, ConfigOK, ChipSelects, Mutation
VARIABLE s
vars == <<s>>
Stages == <<"alloc", "map", "irq-number", "caps", "ahb", "mod", "ecc", "mbus",
    "reset-get", "deassert", "reset", "irq", "dma", "chip1-alloc", "chip1-scan",
    "chip1-register", "chip2-alloc", "chip2-scan", "chip2-register">>
Managed == {"alloc","map","ahb","mod","ecc","mbus","reset-get","irq",
            "chip1-alloc","chip2-alloc","descriptor"}
Init == s = [pc |-> "probe", stage |-> 1, devres |-> <<>>, channel |-> FALSE,
    desc |-> FALSE, dma |-> "pio", reset |-> TRUE, scanned |-> {}, registered |-> {},
    assigned |-> {}, result |-> "unset", cleanup |-> 0]
Step ==
    /\ s.pc = "probe" /\ s.stage <= Len(Stages)
    /\ LET item == Stages[s.stage]
           chip == IF s.stage >= 17 THEN 2 ELSE 1
           select == ChipSelects[chip]
           badcs == item \in {"chip1-scan","chip2-scan"} /\
                    (select > 7 \/ select \in s.assigned)
           error == s.stage = FailAt \/ badcs \/
                    (item = "dma" /\ Backend = "external" /\ Request = "defer")
           enabled == IF Backend = "descriptor" THEN MaskOK /\ AllocOK
                      ELSE IF Backend = "external" THEN Request = "ok" ELSE TRUE
       IN s' = IF error THEN [s EXCEPT !.pc = "unregister", !.result = "error"] ELSE
           [s EXCEPT !.stage = @ + 1,
            !.devres = IF item \in Managed THEN Append(@,item)
                ELSE IF item = "dma" /\ Backend = "descriptor" /\ enabled
                     THEN Append(@,"descriptor") ELSE @,
            !.reset = IF item = "deassert" THEN FALSE ELSE @,
            !.channel = @ \/ (item = "dma" /\ Backend = "external" /\ enabled),
            !.desc = @ \/ (item = "dma" /\ Backend = "descriptor" /\ enabled),
            !.dma = IF item = "dma" /\ enabled THEN Backend ELSE @,
            !.scanned = IF item \in {"chip1-scan","chip2-scan"} THEN @ \cup {chip} ELSE @,
            !.assigned = IF item \in {"chip1-scan","chip2-scan"} THEN @ \cup {select} ELSE @,
            !.registered = IF item \in {"chip1-register","chip2-register"}
                           THEN @ \cup {chip} ELSE @]
Success ==
    /\ s.pc = "probe" /\ s.stage > Len(Stages)
    /\ s' = [s EXCEPT !.pc = "remove", !.result = "ok"]
Remove ==
    /\ s.pc = "remove"
    /\ s' = [s EXCEPT !.pc = "unregister"]
Unregister ==
    /\ s.pc = "unregister"
    /\ s' = [s EXCEPT !.registered = {},
        !.scanned = IF Mutation = "leak-scanned-chip" THEN @ ELSE {},
        !.cleanup = Cardinality(s.scanned), !.pc = "manual"]
ManualCleanup ==
    /\ s.pc = "manual"
    /\ s' = [s EXCEPT !.channel = IF Mutation = "leak-channel" THEN @ ELSE FALSE,
        !.reset = TRUE, !.pc = "devres"]
Devres ==
    /\ s.pc = "devres" /\ Len(s.devres) > 0
    /\ s' = [s EXCEPT !.desc = IF s.devres[Len(s.devres)] = "descriptor" THEN FALSE ELSE @,
        !.devres = SubSeq(@,1,Len(@)-1)]
Finish ==
    /\ s.pc = "devres" /\ Len(s.devres) = 0
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Step \/ Success \/ Remove \/ Unregister \/ ManualCleanup \/ Devres \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
RegisteredScanned == s.registered \subseteq s.scanned
NoLeaks == s.pc = "done" => s.scanned = {} /\ s.registered = {} /\
    ~s.channel /\ ~s.desc /\ Len(s.devres) = 0 /\ s.reset
DmaOwnership == s.pc = "remove" =>
    (s.dma = "external" => s.channel) /\ (s.dma = "descriptor" => s.desc)
UniqueChipSelects == s.pc = "remove" =>
    Cardinality(s.assigned) = Len(ChipSelects)
DmaConfigured == s.pc = "remove" /\ s.dma = "external" => ConfigOK
Terminates == <> (s.pc = "done")
=============================================================================
