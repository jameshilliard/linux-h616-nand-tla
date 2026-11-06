--------------------- MODULE NandDeferredInitialization ---------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Probe lifecycle abstraction, paired with source-executed lifecycle tests.
\* ECC geometry is unchanged between the two ordinary-format ops tables.
EXTENDS Integers, FiniteSets
CONSTANTS EarlyScramble, LateScramble, Vendor, SLC, FailAt, Mutation
VARIABLE s
vars == <<s>>
Policy(scramble) == IF Vendor THEN "vendor"
                   ELSE IF scramble THEN "scrambled" ELSE "plain"
Geometry(ops) == IF ops = "vendor" THEN "packed" ELSE "ordinary"
Init == s = [pc |-> "identify", scramble |-> FALSE, ops |-> "unset",
             geometry |-> "unset", installs |-> 0, installedOps |-> "unset",
             resources |-> {}, finalized |-> FALSE, bbtCalls |-> 0,
             bbtOK |-> FALSE, published |-> FALSE, subpages |-> FALSE]
Identify ==
    /\ s.pc = "identify"
    /\ s' = [s EXCEPT !.scramble = EarlyScramble,
        !.resources = {"ident"},
        !.pc = IF FailAt = "identify" THEN "cleanup" ELSE "attach"]
Attach ==
    /\ s.pc = "attach"
    /\ s' = [s EXCEPT !.scramble = @ \/ Vendor,
        !.geometry = IF Mutation = "wrong-geometry" THEN "packed"
                     ELSE Geometry(Policy(s.scramble)),
        !.ops = IF Mutation = "install-during-attach" THEN Policy(s.scramble) ELSE @,
        !.installs = IF Mutation = "install-during-attach" THEN @ + 1 ELSE @,
        !.resources = @ \cup {"attach"},
        !.pc = IF FailAt = "attach" THEN "cleanup" ELSE "manufacturer"]
Manufacturer ==
    /\ s.pc = "manufacturer"
    /\ s' = [s EXCEPT !.scramble = @ \/ LateScramble,
        !.resources = @ \cup {"manufacturer"},
        !.pc = IF FailAt = "manufacturer" THEN "cleanup" ELSE "tail"]
Tail ==
    /\ s.pc = "tail"
    /\ s' = [s EXCEPT !.resources = @ \cup {"core"},
        \* Core subpage setup observes NO_SUBPAGE_WRITE set during attach.
        !.subpages = SLC /\ ~EarlyScramble /\ ~Vendor,
        !.pc = IF FailAt = "tail" THEN "cleanup"
               ELSE IF Mutation = "scan-bbt-too-early" THEN "bbt"
               ELSE "finalize"]
Finalize ==
    /\ s.pc = "finalize"
    /\ s' = [s EXCEPT !.finalized = TRUE,
        !.installs = IF Mutation = "omit-final-selection" THEN @ ELSE @ + 1,
        !.installedOps = Policy(s.scramble),
        !.ops = IF Mutation = "omit-final-selection" THEN @
                ELSE IF Mutation = "replace-vendor" THEN "scrambled"
                ELSE Policy(s.scramble),
        !.pc = IF Mutation = "publish-before-bbt" THEN "publish" ELSE "bbt"]
BBT ==
    /\ s.pc = "bbt"
    /\ s' = [s EXCEPT !.bbtCalls = @ + 1,
        !.installs = IF Mutation = "reinstall-before-bbt" THEN @ + 1 ELSE @,
        !.ops = IF Mutation = "change-after-install" THEN "plain" ELSE @,
        !.resources = IF FailAt = "bbt" THEN @ ELSE @ \cup {"bbt"},
        !.bbtOK = FailAt \notin {"bbt", "bbt-after-alloc"},
        !.pc = IF FailAt \in {"bbt", "bbt-after-alloc"} THEN "cleanup"
               ELSE IF Mutation = "duplicate-bbt" /\ s.bbtCalls = 0
                    THEN "bbt" ELSE "publish"]
Publish ==
    /\ s.pc = "publish"
    /\ s' = [s EXCEPT !.published = FailAt # "publish", !.pc = "remove"]
Remove ==
    /\ s.pc = "remove"
    /\ s' = [s EXCEPT !.published = FALSE, !.pc = "cleanup"]
Cleanup ==
    /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.resources =
        IF Mutation = "leak-partial-bbt" /\ FailAt = "bbt-after-alloc"
        THEN @ \cap {"bbt"} ELSE {}, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Identify \/ Attach \/ Manufacturer \/ Tail \/ Finalize \/ BBT \/
        Publish \/ Remove \/ Cleanup \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
TypeOK == s.ops \in {"unset", "plain", "scrambled", "vendor"} /\
          s.installedOps \in {"unset", "plain", "scrambled", "vendor"} /\
          s.geometry \in {"unset", "ordinary", "packed"} /\ s.installs \in 0..2 /\
          s.resources \subseteq {"ident", "attach", "manufacturer", "core", "bbt"} /\
          s.scramble \in BOOLEAN /\ s.finalized \in BOOLEAN /\
          s.bbtOK \in BOOLEAN /\ s.published \in BOOLEAN /\ s.bbtCalls \in 0..2
FinalBeforeBBT == s.bbtCalls > 0 => s.finalized /\ s.ops = Policy(s.scramble)
InstallOnce == s.installs <= 1 /\
               (~s.finalized => s.installs = 0 /\ s.ops = "unset") /\
               (s.finalized => s.installs = 1)
StablePolicy == s.finalized => s.ops = s.installedOps
GeometryCompatible == s.finalized => s.geometry = Geometry(s.ops)
OneBBT == s.bbtCalls <= 1
PublishReady == s.published => s.finalized /\ s.bbtOK /\ s.bbtCalls = 1
NoLeak == s.pc = "done" => s.resources = {} /\ ~s.published
SubpagePolicy == s.bbtCalls > 0 /\ s.scramble => ~s.subpages
Terminates == <> (s.pc = "done")
=============================================================================
