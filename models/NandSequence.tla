---------------------------- MODULE NandSequence ----------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Persistent register context across raw, ECC, OOB, status and failed requests.
\* Data-path calls are serialized; the hardware drains at the declared boundary.
EXTENDS Integers, Sequences
CONSTANTS Operations, FailAt, Mutation
VARIABLE s
vars == <<s>>
Init == s = [pc |-> "select", n |-> 1, chip |-> 0, seed |-> 0,
    random |-> FALSE, ecc |-> FALSE, column |-> 0, spare |-> 0,
    unsafe |-> FALSE, count |-> 0, captured |-> 0]
Op == Operations[s.n]
Select == /\ s.pc = "select" /\ s.n <= Len(Operations)
    /\ s' = [s EXCEPT !.chip = Op.chip, !.column = Op.column,
        !.spare = IF Mutation = "stale-spare" THEN @ ELSE Op.spare,
        !.ecc = Op.kind = "ecc", !.pc = "configure"]
Configure == /\ s.pc = "configure"
    /\ s' = [s EXCEPT !.seed = IF Op.kind = "ecc" /\ Mutation # "stale-seed"
                              THEN Op.page ELSE @,
        !.random = IF Op.kind = "ecc" THEN TRUE ELSE @, !.pc = "transfer"]
Transfer == /\ s.pc = "transfer"
    /\ s' = [s EXCEPT !.unsafe = @ \/ s.chip # Op.chip \/
        s.column # Op.column \/ s.spare # Op.spare \/
        (Op.kind = "ecc" /\ (~s.ecc \/ ~s.random \/ s.seed # Op.page)) \/
        (Op.kind # "ecc" /\ (s.ecc \/ s.random)),
        !.captured = IF s.n = FailAt THEN @ ELSE @ + 1, !.pc = "cleanup"]
Cleanup == /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.ecc = FALSE,
        !.random = IF Mutation = "dirty-error" /\ s.n = FailAt THEN @ ELSE FALSE,
        !.count = @ + 1, !.n = @ + 1, !.pc = "select"]
Finish == /\ s.pc = "select" /\ s.n > Len(Operations)
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Select \/ Configure \/ Transfer \/ Cleanup \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
CurrentContext == ~s.unsafe
NoInheritedEnable == s.pc \in {"select", "done"} => ~s.ecc /\ ~s.random
CompletedPrefix == s.pc = "done" => s.count = Len(Operations) /\
    s.captured = Len(Operations) - (IF FailAt \in 1..Len(Operations) THEN 1 ELSE 0)
Terminates == <> (s.pc = "done")
=============================================================================
