------------------------ MODULE MtdParserReferences ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Metadata-only parsers of a one/two-level partition chain. Registration
\* and deletion use the partition mutex; parsing runs outside that mutex.
\* Device/data references and provider admission are separate contracts.
EXTENDS Naturals, FiniteSets
CONSTANTS Depth, Target, InitiallyBusy, ConcurrentDelete, ParserPin,
          EarlyPut, PutKind, CheckOpen, RejectRemovedParent
Nodes == 1..Depth
Bool(x) == IF x THEN 1 ELSE 0
ASSUME /\ Depth \in {1, 2} /\ Target \in Nodes
       /\ ParserPin \in {"none", "metadata", "operational"}
       /\ PutKind \in {"matching", "none", "metadata", "operational"}
       /\ InitiallyBusy \in BOOLEAN /\ ConcurrentDelete \in BOOLEAN
       /\ EarlyPut \in BOOLEAN /\ CheckOpen \in BOOLEAN
       /\ RejectRemovedParent \in BOOLEAN
VARIABLE s
vars == <<s>>

Init == s = [pc |-> [n \in Nodes |-> IF n = 1 THEN "pin" ELSE "new"],
    pins |-> [n \in Nodes |-> "none"], result |-> [n \in Nodes |-> "ok"],
    registered |-> {1}, alive |-> {1}, lock |-> 1, removed |-> FALSE,
    busy |-> InitiallyBusy, user |-> 0, userHeld |-> FALSE,
    opGets |-> 0, opPuts |-> 0, metaGets |-> 0, metaPuts |-> 0,
    badStorage |-> FALSE, badOpen |-> FALSE, badRecovery |-> FALSE]

\* An operational handle retains the device chain; a metadata reference
\* retains only backing data. A living child retains its parent's data.
Retained(n) == n \in s.registered \/ s.pins[n] = "metadata" \/
    (\E k \in n..Depth: s.pins[k] = "operational") \/
    (s.userHeld /\ Target >= n) \/ (n < Depth /\ n + 1 \in s.alive)

Pin(n) ==
    /\ s.pc[n] = "pin" /\ s.lock = n
    /\ LET refused == ParserPin = "operational" /\ s.busy
           held == IF refused \/ EarlyPut THEN "none" ELSE ParserPin
       IN s' = [s EXCEPT !.pc[n] = IF refused THEN "return" ELSE "parse",
           !.pins[n] = held, !.result[n] = IF refused THEN "busy" ELSE "ok",
           !.lock = 0, !.badStorage = @ \/ n \notin s.alive,
           !.opGets = @ + Bool(ParserPin = "operational" /\ ~refused),
           !.opPuts = @ + Bool(ParserPin = "operational" /\ ~refused /\ EarlyPut),
           !.metaGets = @ + Bool(ParserPin = "metadata"),
           !.metaPuts = @ + Bool(ParserPin = "metadata" /\ EarlyPut)]

Parse(n) ==
    /\ s.pc[n] = "parse" /\ s.lock = 0
    /\ LET child == n < Depth /\
                    (n \in s.registered \/ ~RejectRemovedParent)
       IN s' = [s EXCEPT
           !.pc = IF child THEN [@ EXCEPT ![n] = "wait", ![n + 1] = "pin"]
                           ELSE [@ EXCEPT ![n] = "return"],
           !.lock = IF child THEN n + 1 ELSE 0,
           !.registered = IF child THEN @ \cup {n + 1} ELSE @,
           !.alive = IF child THEN @ \cup {n + 1} ELSE @,
           !.result[n] = IF n \in s.registered THEN "ok" ELSE "removed",
           !.badStorage = @ \/ n \notin s.alive]

ChildReturn(n) ==
    /\ n < Depth /\ s.pc[n] = "wait" /\ s.pc[n + 1] = "done"
    /\ s' = [s EXCEPT !.pc[n] = "return", !.result[n] = s.result[n + 1]]

Return(n) ==
    /\ s.pc[n] = "return" /\ s.lock = 0
    /\ LET release == IF PutKind = "matching" THEN s.pins[n]
                      ELSE IF s.pins[n] = "none" THEN "none" ELSE PutKind
       IN s' = [s EXCEPT !.pc[n] = "done",
           !.pins[n] = IF release = s.pins[n] THEN "none" ELSE @,
           !.result[n] = IF n \in s.registered THEN @ ELSE "ok",
           !.opPuts = @ + Bool(release = "operational"),
           !.metaPuts = @ + Bool(release = "metadata"),
           !.badStorage = @ \/ n \notin s.alive]

\* A client still needs an operational open. Try once while the provider
\* is busy, then retry after that transient condition clears.
Open ==
    /\ s.pc[1] = "done" /\ s.user \in {0, 3} /\ s.lock = 0
    /\ LET accepted == Target \in s.registered /\ (~s.busy \/ ~CheckOpen)
       IN s' = [s EXCEPT !.user = @ + 1, !.userHeld = accepted,
           !.opGets = @ + Bool(accepted),
           !.badOpen = @ \/ (s.busy /\ accepted),
           !.badRecovery = @ \/ (s.user = 3 /\ ~s.removed /\ ~accepted)]

Close ==
    /\ s.user \in {1, 4}
    /\ s' = [s EXCEPT !.user = @ + 1, !.userHeld = FALSE,
                      !.opPuts = @ + Bool(s.userHeld)]

Recover ==
    /\ s.user = 2
    /\ s' = [s EXCEPT !.user = 3, !.busy = FALSE]

Delete ==
    /\ ~s.removed /\ s.lock = 0
    /\ ConcurrentDelete \/ (s.pc[1] = "done" /\ s.user = 5)
    /\ s' = [s EXCEPT !.registered = {}, !.removed = TRUE]

Free(n) ==
    /\ n \in s.alive /\ ~Retained(n)
    /\ s' = [s EXCEPT !.alive = @ \ {n}]

Finished == s.pc[1] = "done" /\ s.user = 5 /\ s.removed /\ s.alive = {}
Done == Finished /\ UNCHANGED vars
Next == (\E n \in Nodes: Pin(n) \/ Parse(n) \/ ChildReturn(n) \/ Return(n) \/ Free(n))
        \/ Open \/ Close \/ Recover \/ Delete \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)

TypeOK == /\ s.pc \in [Nodes -> {"new", "pin", "parse", "wait", "return", "done"}]
          /\ s.pins \in [Nodes -> {"none", "metadata", "operational"}]
          /\ s.result \in [Nodes -> {"ok", "busy", "removed"}]
          /\ s.registered \subseteq Nodes /\ s.alive \subseteq Nodes
          /\ s.lock \in 0..Depth /\ s.user \in 0..5
          /\ s.opGets \in Nat /\ s.opPuts \in Nat
          /\ s.metaGets \in Nat /\ s.metaPuts \in Nat
ParserStorage == ~s.badStorage
NoOrphanChild == \A n \in 2..Depth: n \in s.registered => n - 1 \in s.registered
RegistrationIndependent == s.pc[1] = "done" /\ ~s.removed =>
    s.registered = Nodes /\ s.result[1] = "ok"
OpenAdmission == ~s.badOpen
OpenAfterRecovery == ~s.badRecovery
ProviderBalance == s.opGets = s.opPuts + Bool(s.userHeld) +
    Cardinality({n \in Nodes: s.pins[n] = "operational"})
MetadataBalance == s.metaGets = s.metaPuts +
    Cardinality({n \in Nodes: s.pins[n] = "metadata"})
PinsDrained == s.pc[1] = "done" => \A n \in Nodes: s.pins[n] = "none"
Terminates == <>Finished
=============================================================================
