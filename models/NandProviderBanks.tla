------------------------- MODULE NandProviderBanks -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Non-cyclic IRQ queue-complete events, one active descriptor per channel.
\* SourceDispatch reproduces the pinned sun6i bank loop/index arithmetic.
EXTENDS Integers
CONSTANTS Channels, SourceDispatch, Mutation
VARIABLE s
vars == <<s>>
Init == s = [channel |-> 0, wrong |-> FALSE, delivered |-> {}]
Dispatch == /\ s.channel < Channels
    /\ LET bank == s.channel \div 8
           banks == IF SourceDispatch THEN Channels \div 8 ELSE (Channels + 7) \div 8
           recipient == IF SourceDispatch THEN s.channel % 8 ELSE s.channel
       IN s' = [s EXCEPT !.wrong = @ \/ bank >= banks \/ recipient # s.channel,
            !.delivered = IF bank < banks THEN @ \cup {recipient} ELSE @,
            !.channel = @ + 1]
Done == s.channel = Channels /\ UNCHANGED vars
Next == Dispatch \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
CorrectRecipient == ~s.wrong
CompleteCoverage == s.channel = Channels => s.delivered = 0..(Channels-1)
Terminates == <> (s.channel = Channels)
=============================================================================
