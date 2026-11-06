------------------------ MODULE NandRandomizerPolicy ------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Actual C helper observations, checked against independently encoded input.
\* Full nand_scan/BBT ordering is checked by NandDeferredInitialization.
\* NAND transport and hardware decoding are explicit fixtures, not executed.
EXTENDS NandRandomizerMath, TLC, TLCExt
CONSTANTS Records, PageSeeds, Ecc512, Ecc1024, Mutation
VARIABLE s
vars == <<s>>
\* Only immutable input/arithmetic is cached; TLCCache is the identity operator.
InputFor(i) == TLCCache(Records[i], i)
KeyFor(seed) == TLCCache(Mask(seed, 4), seed)
R == InputFor(s.index)
O == R.observed
Random == R.mode # 0
Compensate == R.mode \in {1, 2} /\ R.bbm
Index == 1 + (R.page % (IF R.ppb < 128 THEN R.ppb ELSE 128))
EccSeed == IF R.step = 512 THEN Ecc512[Index] ELSE Ecc1024[Index]
Key == KeyFor(EccSeed)
CompKey == [i \in 1..4 |-> IF Compensate /\ i <= 2 THEN Key[i] ELSE 0]
UserInput == Apply(R.marker, CompKey)
\* Ordinary BBMs are plain on media; the vendor format stores them scrambled.
WireMask == [i \in 1..4 |-> IF Random THEN Key[i] ELSE 0]
ExpectedMedia == [i \in 1..4 |->
    IF Random /\ ~(Compensate /\ i <= 2) THEN XorByte(R.marker[i], Key[i])
    ELSE R.marker[i]]
SeedField(n) == (n \div 65536) % 32768
\* Preserve bit 31 explicitly without exceeding TLC's signed integer range.
SetSeed(n, seed) == [n EXCEPT !.lo = @ - SeedField(@) * 65536 + seed * 65536]
SetEnable(n) == [n EXCEPT !.lo = IF Bit(@, R.enableBit) = 1 THEN @ ELSE @ + 2^R.enableBit]
Configured(before, ecc) == IF Random
    THEN SetSeed(before, IF ecc THEN EccSeed ELSE PageSeeds[Index]) ELSE before
Init == s = [index |-> 1, pc |-> "select", policyOK |-> TRUE,
    seedOK |-> TRUE, registerOK |-> TRUE, readOK |-> TRUE,
    writeOK |-> TRUE, clean |-> TRUE, transportOK |-> TRUE]
Select == /\ s.pc = "select"
    /\ s' = [s EXCEPT !.pc = "configure",
        !.policyOK = @ /\ O.randomized = Random /\
                     O.compensate = (R.mode \in {1, 2})]
Configure == /\ s.pc = "configure"
    /\ s' = [s EXCEPT !.pc = "read",
        !.seedOK = @ /\ O.pageSeed = PageSeeds[Index] /\ O.eccSeed = EccSeed,
        !.registerOK = @ /\ \A i \in 1..2:
            LET reg == O.regs[i]
                value == Configured(reg.before, i = 2)
            IN reg.configured = value /\ reg.on = (IF Random THEN SetEnable(value) ELSE value)
               /\ reg.off = value]
Read == /\ s.pc = "read"
    /\ s' = [s EXCEPT !.pc = "write",
        !.readOK = @ /\ R.input = UserInput /\ O.rx = R.marker]
Write == /\ s.pc = "write"
    /\ s' = [s EXCEPT !.pc = "cleanup",
        !.writeOK = @ /\ O.tx = UserInput /\ Apply(O.tx, WireMask) = ExpectedMedia]
Cleanup == /\ s.pc = "cleanup"
    /\ s' = [s EXCEPT !.pc = "next",
        !.clean = @ /\ \A transfer \in {O.read, O.write}:
                          Bit(transfer.after.lo, R.enableBit) = 0,
        !.transportOK = @ /\ \A transfer \in {O.read, O.write}:
            LET value == Configured(transfer.before, transfer.ecc)
            IN transfer.during = (IF Random THEN SetEnable(value) ELSE value)
               /\ transfer.after = value /\ transfer.ret = R.fail]
NextRecord == /\ s.pc = "next"
    /\ s' = [s EXCEPT !.index = IF @ < Len(Records) THEN @ + 1 ELSE @,
        !.pc = IF s.index < Len(Records) THEN "select" ELSE "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Select \/ Configure \/ Read \/ Write \/ Cleanup \/ NextRecord \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
Policy == s.policyOK
Seeds == s.seedOK
Registers == s.registerOK
ReadCompatibility == s.readOK
WriteCompatibility == s.writeOK
ReturnedClean == s.clean
Transport == s.transportOK
Terminates == <> (s.pc = "done")
=============================================================================
