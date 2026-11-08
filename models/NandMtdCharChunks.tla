------------------------- MODULE NandMtdCharChunks -------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* MEMREAD/MEMWRITE chunk protocol. NAND programming, ECC decoding and device
\* lifetime are outside this model. Byte identity is checked by address, not
\* by a write/read round trip. Each Page action represents one backend page.
EXTENDS Integers, Sequences, FiniteSets
CONSTANTS Requests, Faults, Mutation, CompleteOob
VARIABLES r, s
vars == <<r, s>>
Min(a,b) == IF a < b THEN a ELSE b
Max(a,b) == IF a > b THEN a ELSE b
Cover(a,n,w) == IF n = 0 THEN 0 ELSE ((a % w) + n - 1) \div w + 1
Ocap == IF Mutation = "small-oob-buffer" THEN r.erase
        ELSE Max(r.erase,r.spare)
Dbuf == Min(r.data,r.erase)
Obuf == Min(r.oob,Ocap)
More == s.dleft > 0 \/ ((CompleteOob \/ ~r.hasData) /\ s.oleft > 0)
BackendMore == s.rd < s.pd \/
    ((r.symmetric \/ (IF CompleteOob THEN s.pd = 0 ELSE ~r.hasData)) /\ s.ro < s.po)
NextOffset(a,d,o) ==
    IF Mutation = "no-oob-advance" THEN a+d
    ELSE IF CompleteOob THEN
        IF o = 0 THEN a+d
        ELSE Max(a+d,a-(a % r.width)+((o-1) \div r.avail+1)*r.width)
    ELSE a + IF r.hasData THEN d ELSE (o \div r.avail)*r.width

\* The source changes these limits in this order. The identity oracle below
\* does not use this planner to decide which bytes a request should address.
Plan(a,d,o) ==
    LET d0 == Min(d,Dbuf)
        d1 == IF d0 = r.erase /\ Mutation # "unaligned-data-chunk"
              THEN d0 - ((a+d0) % r.width) ELSE d0
        o0 == Min(o,Obuf)
        split == o0 < o /\ r.avail > 0
        o1 == IF split /\ Mutation # "split-oob-page"
              THEN o0 - (o0 % r.avail) ELSE o0
        d2 == IF split /\ d1 > 0 /\ Mutation # "uncoupled-data"
              THEN Min(d1,(o1 \div r.avail)*r.width - (a % r.width))
              ELSE d1
        limit == ~r.read \/ ((CompleteOob \/ d2 < d) /\ Mutation # "uncoupled-read-oob")
        o2 == IF limit /\ d2 > 0 /\ o1 > 0
              THEN Min(o1,Cover(a,d2,r.width)*r.avail) ELSE o1
        o3 == IF CompleteOob /\ ~r.read /\ d2 = 0
              THEN Min(o2,r.avail) ELSE o2
    IN [data |-> d2, oob |-> o3]

Init ==
    /\ r \in {Requests[i] : i \in 1..Len(Requests)}
    /\ s = [pc |-> "validate", at |-> r.start,
            dleft |-> r.data, oleft |-> r.oob, dd |-> 0, od |-> 0,
            pd |-> 0, po |-> 0, rd |-> 0, ro |-> 0,
            row |-> 0, col |-> 0, owned |-> {}, seen |-> {},
            mapping |-> TRUE, unique |-> TRUE, progress |-> TRUE,
            corrected |-> FALSE, bad |-> FALSE, code |-> "ok"]

Validate ==
    /\ UNCHANGED r
    /\ s.pc = "validate"
    /\ s' = [s EXCEPT !.pc = IF r.start+r.data > r.size
                            THEN "finish" ELSE "alloc-data",
                      !.code = IF r.start+r.data > r.size
                               THEN "invalid" ELSE "ok"]
AllocateData ==
    /\ UNCHANGED r
    /\ s.pc = "alloc-data"
    /\ s' = [s EXCEPT !.pc = "alloc-oob",
                      !.owned = IF Dbuf > 0 THEN @ \cup {"data"} ELSE @]
AllocateOob ==
    /\ UNCHANGED r
    /\ s.pc = "alloc-oob"
    /\ s' = [s EXCEPT !.pc = "loop",
                      !.owned = IF Obuf > 0 THEN @ \cup {"oob"} ELSE @]
AllocationFailure ==
    /\ UNCHANGED r
    /\ Faults
    /\ ((s.pc = "alloc-data" /\ Dbuf > 0) \/
        (s.pc = "alloc-oob" /\ Obuf > 0))
    /\ s' = [s EXCEPT !.pc = "finish", !.code = "nomem"]
Loop ==
    /\ UNCHANGED r
    /\ s.pc = "loop"
    /\ s' = [s EXCEPT !.pc = IF More THEN "plan" ELSE "finish"]
Prepare ==
    /\ UNCHANGED r
    /\ s.pc = "plan"
    /\ IF s.oleft > 0 /\ r.avail = 0
          THEN s' = [s EXCEPT !.pc = "finish", !.code = "invalid"]
          ELSE LET p == Plan(s.at,s.dleft,s.oleft)
               IN s' = [s EXCEPT !.pd = p.data, !.po = p.oob,
                                 !.rd = 0, !.ro = 0,
                                 !.row = s.at \div r.width,
                                 !.col = s.at % r.width,
                                 !.pc = IF r.read THEN "bounds" ELSE "copy-data"]
CopyData ==
    /\ UNCHANGED r
    /\ s.pc = "copy-data"
    /\ s' = [s EXCEPT !.pc = "copy-oob"]
CopyOob ==
    /\ UNCHANGED r
    /\ s.pc = "copy-oob"
    /\ s' = [s EXCEPT !.pc = IF r.read THEN "account" ELSE "bounds"]
CopyFailure ==
    /\ UNCHANGED r
    /\ Faults /\ s.pc \in {"copy-data", "copy-oob"}
    /\ s' = [s EXCEPT !.pc = "finish", !.code = "fault"]
Bounds ==
    /\ UNCHANGED r
    /\ s.pc = "bounds"
    /\ LET invalid == s.at+s.pd > r.size \/
                      s.po > (r.size \div r.width - s.row)*r.avail \/
                      (~r.read /\ ~r.symmetric /\
                       (IF CompleteOob THEN s.pd = 0 ELSE ~r.hasData) /\ s.po > r.avail)
       IN s' = [s EXCEPT !.pc = IF invalid THEN "finish" ELSE "backend",
                         !.code = IF invalid THEN "invalid" ELSE "ok"]
Page ==
    /\ UNCHANGED r
    /\ s.pc = "backend" /\ BackendMore
    /\ LET d == Min(s.pd-s.rd,r.width-s.col)
           o == Min(s.po-s.ro,r.avail)
           dataOK == d = 0 \/ s.row*r.width+s.col = r.start+s.dd+s.rd
           oobOK == o = 0 \/ s.row*r.avail =
                            (r.start \div r.width)*r.avail+s.od+s.ro
       IN s' = [s EXCEPT !.rd = @+d, !.ro = @+o,
                         !.mapping = @ /\ dataOK /\ oobOK,
                         !.unique = @ /\ s.row \notin s.seen,
                         !.seen = @ \cup {s.row}, !.row = @+1, !.col = 0]
BackendFailure ==
    /\ UNCHANGED r
    /\ Faults /\ s.pc = "backend" /\ BackendMore
    /\ s' = [s EXCEPT !.pc = "finish", !.code = "io"]
BackendReturn ==
    /\ UNCHANGED r
    /\ s.pc = "backend" /\ ~BackendMore
    /\ \E ecc \in IF r.read /\ Faults THEN {"ok", "clean", "bad"} ELSE {"ok"} :
        s' = [s EXCEPT !.pc = IF r.read THEN "copy-data" ELSE "account",
                        !.code = ecc,
                        !.corrected = @ \/ ecc = "clean", !.bad = @ \/ ecc = "bad"]
\* These are deliberately weakened backend contracts, not claimed behavior
\* of a specific driver: the ioctl cannot safely resume arbitrary short I/O.
ShortSuccess ==
    /\ UNCHANGED r
    /\ Mutation \in {"zero-success", "short-success"}
    /\ s.pc = "backend" /\ BackendMore
    /\ s.rd+s.ro = 0
    /\ s' = [s EXCEPT !.pc = IF r.read THEN "copy-data" ELSE "account",
                      !.ro = IF Mutation = "short-success" THEN Min(s.po,1) ELSE 0]
Account ==
    /\ UNCHANGED r
    /\ s.pc = "account"
    /\ s' = [s EXCEPT !.at = NextOffset(s.at,s.rd,s.ro),
                      !.dleft = @-s.rd, !.oleft = @-s.ro,
                      !.dd = @+s.rd, !.od = @+s.ro,
                      !.progress = @ /\ s.rd+s.ro > 0, !.pc = "loop"]
Finish ==
    /\ UNCHANGED r
    /\ s.pc = "finish"
    /\ s' = [s EXCEPT !.pc = "done",
                      !.owned = IF Mutation = "leak-oob" THEN @ \cap {"oob"} ELSE {},
                      !.code = IF r.read /\ s.code \in {"ok","clean"}
                               THEN IF s.bad THEN "bad"
                                    ELSE IF s.corrected THEN "clean" ELSE "ok"
                               ELSE @]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Validate \/ AllocateData \/ AllocateOob \/ AllocationFailure \/ Loop \/
        Prepare \/ CopyData \/ CopyOob \/ CopyFailure \/ Bounds \/ Page \/
        BackendFailure \/ BackendReturn \/ ShortSuccess \/ Account \/ Finish \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)

BufferBounds == /\ s.pd \in 0..Dbuf /\ s.po \in 0..Obuf
                /\ s.rd \in 0..s.pd /\ s.ro \in 0..s.po
                /\ s.dd \in 0..r.data /\ s.od \in 0..r.oob
                /\ s.dd+s.dleft = r.data /\ s.od+s.oleft = r.oob
ByteIdentity == s.mapping
ExactlyOnce == s.unique
Progress == s.progress
OwnedDuringIO == s.pc \in {"copy-data","copy-oob","bounds","backend","account"} =>
                 ((s.pd = 0 \/ "data" \in s.owned) /\
                  (s.po = 0 \/ "oob" \in s.owned))
ReturnedClean == s.pc = "done" => s.owned = {}
Complete == (s.pc = "done" /\ s.code \in {"ok","clean","bad"} /\
             (CompleteOob \/ ((r.data > 0 \/ ~r.hasData) /\
              (~r.hasData \/ r.oob <= Cover(r.start,r.data,r.width)*r.avail)))) =>
            (s.dd = r.data /\ s.od = r.oob)
EccPriority == (s.pc = "done" /\ s.code \in {"ok","clean","bad"}) =>
              (s.bad => s.code = "bad") /\
              (~s.bad /\ s.corrected => s.code = "clean")
Terminates == <> (s.pc = "done")
=============================================================================
