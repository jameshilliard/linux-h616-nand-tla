#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compound request/IRQ/DMA interleavings and deliberately weak contracts."""
import sys
import run as tlc

MODEL=dict(invariants=["Owner","MappingSafe","NoStaleHandler","FreshCompletion","ReturnedClean"],
           actions=["Launch","Arm","Arrive","Retire","IrqStatus","IrqMaskRead","IrqAck",
                    "IrqMaskWrite","IrqSignal","Wake","Timeout","Drain","Recheck","Clear","Abort","Unmap"])


def cases():
    tests=[]
    for internal in (False,True):
        base=dict(Internal=internal,StopOK=True,RetireBus=True,RetireEvents=True,Mutation="none")
        for name,changes,expected in (
                ("source",{},None),
                ("failed-stop",dict(StopOK=False),"MappingSafe"),
                ("unretired-bus",dict(RetireBus=False),"MappingSafe"),
                ("late-event",dict(RetireEvents=False),"FreshCompletion"),
                ("undrained-irq",dict(Mutation="no-drain"),"NoStaleHandler")):
            tests.append(dict(name=f"request-ordering-{int(internal)}-{name}",module="NandRequestOrdering",
                constants=dict(base,**changes),expected=expected,
                kind="positive" if not expected else "mutation" if name=="undrained-irq" else "assumption"))
    return tests


if __name__=="__main__":
    raise SystemExit(tlc.main(sys.argv[1:],cases=cases(),modules={"NandRequestOrdering":MODEL}))
