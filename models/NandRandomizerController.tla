---------------------- MODULE NandRandomizerController ----------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Register-programmed seed and active LFSR are deliberately separate.
\* Profile alternatives are uncertainty envelopes, NOT silicon discoveries.
\* Events serialize register accesses with commands, as the driver does.
EXTENDS NandRandomizerMath, TLC
CONSTANTS Events, Profile, InitialStates, InitialValidity, Mutation
VARIABLE s
vars == <<s>>
Seed(cfg) == (cfg \div 65536) % 32768
Enabled(cfg) == Bit(cfg, Profile.enableBit) = 1
Page(cfg) == Bit(cfg, Profile.pageBit) = 1
Msb(cfg) == Bit(cfg, Profile.directionBit) = 1
Reverse(n) == Pack([i \in 1..8 |-> Bit(n, 8 - i)], 8)
Load(n) == Advance(n, 15)
LoadBits(n) == BitAdvance(Bits(n), 15)
ResetConfig == 19072 * 65536 + 8
E == Events[s.event]
Init == \E initial \in InitialStates, valid \in InitialValidity :
    s = [event |-> 1, pc |-> "event", cfg |-> ResetConfig,
         active |-> initial, bits |-> Bits(initial), valid |-> valid,
         chip |-> 0, pos |-> 0, output |-> <<>>, copied |-> <<>>,
         exact |-> TRUE, observed |-> TRUE, failed |-> FALSE,
         column |-> 0, commands |-> 0]

\* A control write need not rewind a running page-mode stream. The documented
\* register contents alone cannot establish that hidden-state side effect.
Config == /\ s.pc = "event" /\ s.event <= Len(Events) /\ E.op = "config"
    /\ LET choices == IF Seed(E.value) # Seed(s.cfg) THEN Profile.seedWrite
                      ELSE IF Page(E.value) # Page(s.cfg) \/ Msb(E.value) # Msb(s.cfg)
                           THEN Profile.modeWrite
                      ELSE IF ~Enabled(s.cfg) /\ Enabled(E.value)
                           THEN Profile.enableWrite ELSE Profile.sameWrite
       IN \E effect \in choices :
           LET reload == effect = "reload" \/ Mutation = "register-is-state"
                         \/ (Mutation = "reload-on-enable" /\
                             ~Enabled(s.cfg) /\ Enabled(E.value))
           IN s' = [s EXCEPT !.cfg = E.value, !.event = @ + 1,
               !.active = IF reload THEN Load(Seed(E.value)) ELSE @,
               !.bits = IF reload THEN LoadBits(Seed(E.value)) ELSE @,
               !.valid = IF reload THEN TRUE ELSE IF effect = "invalidate" THEN FALSE ELSE @]

\* NAND reset, controller reset and chip selection are different events.
\* Reset of registers is not treated as proof of a particular internal phase.
Reset == /\ s.pc = "event" /\ s.event <= Len(Events)
    /\ E.op \in {"nand-reset", "soft-reset", "bus-reset", "select"}
    /\ LET choices == CASE E.op = "select" -> Profile.select
                        [] E.op = "nand-reset" -> Profile.nandReset
                        [] OTHER -> Profile.controllerReset
       IN \E effect \in choices :
           s' = [s EXCEPT !.event = @ + 1,
               !.cfg = IF E.op \in {"soft-reset", "bus-reset"} THEN ResetConfig ELSE @,
               !.chip = IF E.op = "select" THEN E.value ELSE @,
               !.valid = IF effect = "invalidate" THEN FALSE ELSE @,
               !.active = IF effect = "zero" THEN 0 ELSE @,
               !.bits = IF effect = "zero" THEN Bits(0) ELSE @]

NormalRestart == IF E.read THEN
                    IF Page(s.cfg) THEN Profile.normalPageRead ELSE Profile.normalBlockRead
                 ELSE IF Page(s.cfg) THEN Profile.normalPageWrite ELSE Profile.normalBlockWrite
Start == /\ s.pc = "event" /\ s.event <= Len(Events) /\ E.op = "command"
    /\ \E restart \in IF E.kind = "NORMAL" THEN NormalRestart ELSE Profile.eccStart :
        LET reload == Enabled(s.cfg) /\ (~s.valid \/
                          (restart /\ Mutation # "continue-command"))
            seed == IF Mutation = "column-is-phase" THEN Advance(Seed(s.cfg), E.column * 8)
                    ELSE Seed(s.cfg)
        IN s' = [s EXCEPT !.pos = 0, !.output = <<>>, !.copied = <<>>,
            !.column = E.column, !.failed = FALSE, !.pc = "bytes",
            !.commands = @ + 1,
            !.active = IF reload THEN Load(seed) ELSE @,
            !.bits = IF reload THEN LoadBits(seed) ELSE @,
            !.valid = @ \/ reload]

\* boundaries are bus-stream offsets, including protected user and parity
\* bytes. BCH generation/correction is external, not reimplemented here.
Byte == /\ s.pc = "bytes" /\ s.pos < Len(E.input)
    /\ LET choices == IF E.kind # "NORMAL" /\ s.pos \in E.boundaries
                      THEN IF Page(s.cfg) THEN Profile.pageBoundary ELSE Profile.blockBoundary
                      ELSE {FALSE}
       IN \E boundary \in choices :
           LET on == Enabled(s.cfg)
               reload == boundary /\ on /\ Mutation # "continue-codeword"
               state == IF reload THEN Load(Seed(s.cfg)) ELSE s.active
               bits == IF reload THEN LoadBits(Seed(s.cfg)) ELSE s.bits
               tail == E.kind = "NORMAL" /\ E.read /\ Len(E.input) % 4 # 0
                       /\ s.pos = Len(E.input) - 1
           IN \E bypass \in IF tail THEN Profile.readTailBypass ELSE {FALSE} :
               LET plain == ~on \/ (bypass /\ Mutation # "randomize-tail")
                   mask == IF plain THEN 0 ELSE IF Msb(s.cfg) /\ Mutation # "ignore-msb"
                           THEN Reverse(state % 256) ELSE state % 256
                   refmask == IF plain THEN 0 ELSE IF Msb(s.cfg)
                              THEN Reverse(Pack(bits, 8)) ELSE Pack(bits, 8)
                   value == XorByte(E.input[s.pos + 1], mask)
                   advances == (on /\ ~(bypass /\ Mutation = "tail-freezes-state"))
                               \/ Mutation = "advance-disabled"
               IN s' = [s EXCEPT !.pos = @ + 1, !.column = @ + 1,
                   !.output = Append(@, value),
                   !.exact = @ /\ value = XorByte(E.input[s.pos + 1], refmask),
                   !.observed = @ /\ (~E.check \/ value = E.expected[s.pos + 1]),
                   !.active = IF advances THEN Advance(state, 8) ELSE state,
                   !.bits = IF advances THEN BitAdvance(bits, 8) ELSE bits]

Finish == /\ s.pc = "bytes" /\ s.pos = Len(E.input)
    /\ s' = [s EXCEPT !.pc = "event", !.event = @ + 1,
        !.copied = IF E.read THEN s.output ELSE <<>>]
\* Every listed partial prefix may abort. Accepted write bytes remain recorded;
\* a failed read does not become a successful returned buffer.
Abort == /\ s.pc = "bytes" /\ s.pos \in E.aborts
    /\ \E effect \in Profile.abort :
        s' = [s EXCEPT !.pc = "event", !.event = @ + 1, !.failed = TRUE,
            !.copied = IF Mutation = "copy-failed-read" THEN s.output ELSE <<>>,
            !.valid = IF effect = "invalidate" THEN FALSE ELSE @,
            !.active = IF effect = "zero" THEN 0 ELSE @,
            !.bits = IF effect = "zero" THEN Bits(0) ELSE @]
End == /\ s.pc = "event" /\ s.event > Len(Events)
    /\ s' = [s EXCEPT !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Config \/ Reset \/ Start \/ Byte \/ Finish \/ Abort \/ End \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
ExactState == s.active = Pack(s.bits, 15) /\ s.exact
ObservedBytes == s.observed
FailedReadNotPublished == s.failed => s.copied = <<>>
Bounds == s.active \in 0..32767 /\ s.cfg \in 0..2147483647
          /\ s.event \in 1..(Len(Events) + 1)
Terminates == <> (s.pc = "done")
=============================================================================
