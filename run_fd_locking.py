#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explore NAND locks, two chip users, close/sync and device unbind."""
import sys
import run as tlc

MODEL=dict(invariants=["LiveLockStorage","OneControllerOwner","CompleteRelease"],
           actions=["Enter","ChipLock","ControllerLock","ControllerUnlock","ChipUnlock","Put","Unbind","Cleanup"])


def cases():
    result=[]
    for same in (False,True):
        for policy,gate,expected in (("last-ref",True,None),("gate",True,None),
                                     ("source",False,"LiveLockStorage"),
                                     ("gate",False,"LiveLockStorage")):
            result.append(dict(name=f"fd-locking-{int(same)}-{policy}-{int(gate)}",
                module="NandFdLocking",constants=dict(SameChip=same,Policy=policy,
                    GateClose=gate,Mutation="none"),expected=expected,
                kind="positive" if not expected else "known-bug" if policy=="source" else "assumption"))
    result.append(dict(name="fd-locking-omitted-controller-lock",module="NandFdLocking",
        constants=dict(SameChip=False,Policy="last-ref",GateClose=True,Mutation="skip-controller-lock"),
        expected="OneControllerOwner",kind="mutation"))
    return result


if __name__=="__main__":
    raise SystemExit(tlc.main(sys.argv[1:],cases=cases(),modules={"NandFdLocking":MODEL}))
