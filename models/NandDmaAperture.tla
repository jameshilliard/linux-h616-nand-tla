-------------------------- MODULE NandDmaAperture --------------------------
\* SPDX-License-Identifier: GPL-2.0-or-later
\* Two 16-bit limbs avoid TLC signed-integer overflow at the 4 GiB boundary.
\* Mask-conforming DMA mappings/allocations are API contracts. Relaxing them
\* produces conditional witnesses, not reachable sunxi bugs by itself.
EXTENDS Integers, Sequences
CONSTANTS Backend, Address, Length, Descriptor, InheritedBits,
          MapHonorsMask, AllocHonorsMask, Mutation
VARIABLE s
vars == <<s>>
Fits(a,n) == n > 0 /\ a[1] \in 0..65535 /\ a[2] >= 0 /\
             a[2] + ((a[1] + n - 1) \div 65536) <= 65535
Words(a) == <<a[1] % 256, a[1] \div 256, a[2] % 256, (a[2] \div 256) % 256>>
Internal == Backend # "external"
Desc == Backend = "descriptor"
Init == s = [pc |-> "mask", bits |-> InheritedBits, allocated |-> FALSE,
    mapped |-> FALSE, maps |-> 0, unmaps |-> 0, launched |-> FALSE,
    buf |-> <<>>, list |-> <<>>, rejected |-> FALSE]
Mask == /\ s.pc = "mask"
    /\ s' = [s EXCEPT !.bits = IF Desc /\ Mutation # "omit-mask" THEN 32 ELSE @,
        !.pc = "allocate"]
Allocate == /\ s.pc = "allocate"
    /\ IF Desc /\ AllocHonorsMask /\
           ((s.bits = 32 /\ ~Fits(Descriptor,16)) \/ Descriptor[1] % 4 # 0)
       THEN s' = [s EXCEPT !.rejected = TRUE, !.pc = "done"]
       ELSE s' = [s EXCEPT !.allocated = Desc, !.pc = "map"]
Map == /\ s.pc = "map"
    /\ IF MapHonorsMask /\ (Length <= 0 \/ (s.bits = 32 /\ ~Fits(Address,Length)))
       THEN s' = [s EXCEPT !.rejected = TRUE, !.pc = "done"]
       ELSE s' = [s EXCEPT !.mapped = TRUE, !.maps = 1, !.pc = "validate"]
Validate == /\ s.pc = "validate"
    /\ s' = IF Desc /\ (Length > 65535 \/ Length % 8 # 0 \/ Address[1] % 4 # 0)
       THEN [s EXCEPT !.rejected = TRUE, !.pc = "unmap"]
       ELSE [s EXCEPT !.pc = "encode"]
Encode == /\ s.pc = "encode"
    /\ s' = [s EXCEPT !.buf = IF Mutation = "cpu-address" THEN Words(<<0,0>>) ELSE Words(Address),
        !.list = Words(Descriptor), !.pc = "launch"]
Launch == /\ s.pc = "launch"
    /\ s' = [s EXCEPT !.launched = TRUE, !.pc = "unmap"]
Unmap == /\ s.pc = "unmap"
    /\ s' = [s EXCEPT !.mapped = FALSE, !.unmaps = @ + 1, !.pc = "done"]
Done == s.pc = "done" /\ UNCHANGED vars
Next == Mask \/ Allocate \/ Map \/ Validate \/ Encode \/ Launch \/ Unmap \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Next)
BufferRepresentable == s.launched /\ Internal => Fits(Address,Length)
DescriptorRepresentable == s.launched /\ Desc => Fits(Descriptor,16) /\ Descriptor[1] % 4 = 0
ExactAddress == s.launched => s.buf = Words(Address) /\ (Desc => s.list = Words(Descriptor))
NoZeroDescriptor == s.launched /\ Desc => Length > 0
BalancedMaps == s.unmaps <= s.maps /\ (s.pc = "done" => s.maps = s.unmaps /\ ~s.mapped)
Terminates == <> (s.pc = "done")
=============================================================================
