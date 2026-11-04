#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute sun6i IRQ/start/tasklet/stop with virt-dma list/callback bodies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from boundary_common import HERE, sources, bodies, check

FUNCTIONS = {
    "virt-dma.h": ["to_virt_chan", "vchan_issue_pending", "vchan_cookie_complete",
        "vchan_vdesc_fini", "vchan_cyclic_callback", "vchan_terminate_vdesc",
        "vchan_next_desc", "vchan_get_all_descriptors", "vchan_synchronize"],
    "virt-dma.c": ["vchan_complete", "vchan_dma_desc_free_list", "vchan_init"],
    "sun6i-dma.c": ["sun6i_dma_start_desc", "sun6i_dma_tasklet",
        "sun6i_dma_interrupt", "sun6i_dma_terminate_all"],
    "sun4i-dma.c": ["find_and_use_pchan", "release_pchan", "configure_pchan",
        "set_pchan_interrupt", "__execute_vchan_pending", "get_next_cyclic_promise",
        "sun4i_dma_terminate_all", "sun4i_dma_interrupt"],
}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-sun4i", type=Path,
                        help="test a local sun4i source without replacing the pinned baseline")
    args = parser.parse_args()
    src = sources()
    text = (HERE/"reference/providers/sun6i-dma.c").read_text()
    pin = json.loads((HERE/"reference/providers/source.json").read_text())
    assert hashlib.sha256(text.encode()).hexdigest() == pin["files"]["sun6i-dma.c"]
    src["sun6i-dma.c"] = text
    defines = "\n".join(re.findall(r"^#define DMA_(?:IRQ_|CHAN_)[^\n]+",text,re.M))
    text4=(HERE/"reference/providers/sun4i-dma.c").read_text()
    assert hashlib.sha256(text4.encode()).hexdigest()==pin["files"]["sun4i-dma.c"]
    if args.candidate_sun4i:
        text4 = args.candidate_sun4i.read_text()
        assert "device_synchronize\t= sun4i_dma_synchronize;" in text4
        FUNCTIONS["sun4i-dma.c"] += ["sun4i_dma_synchronize", "sun4i_dma_free_chan_resources"]
        FUNCTIONS["virt-dma.h"].append("vchan_free_chan_resources")
    defines += f"\n#define FIXED_SUN4I {int(bool(args.candidate_sun4i))}\n"
    src["sun4i-dma.c"]=text4
    defines += "\n"+"\n".join(re.findall(r"^#define SUN4I_[^\n\\]*(?:\\\n[^\n\\]*)*",text4,re.M))
    structs = []
    for name in ("virt_dma_desc", "virt_dma_chan"):
        structs.append(re.search(r"struct " + name + r" \{.*?\n};",src["virt-dma.h"],re.S)[0])
    code, proto, hashes = bodies(src, FUNCTIONS)
    check("provider-boundary", {"check.c":
        (HERE/"tests/boundary_list.h").read_text() +
        (HERE/"tests/provider_boundary.h").read_text().replace("/* @virt-structs@ */", "\n".join(structs)) +
        (HERE/"tests/provider_sun4i.h").read_text() + defines + "\n" + proto + "\n" +
        # The pinned helper forms &vd->tx_result with vd == NULL for an
        # empty cyclic callback; the callback helper never dereferences it.
        # Keep the body intact, disable only this function's null sanitizer.
        'static void vchan_complete(struct tasklet_struct *) __attribute__((no_sanitize("null")));\n' + code +
        '\n#line 1 "provider_sun4i.c"\n'+(HERE/"tests/provider_sun4i.c").read_text()+
        '\n#line 1 "provider_boundary.c"\n'+(HERE/"tests/provider_boundary.c").read_text()}, hashes,
        flags=["-Wno-unused-but-set-variable"], coverage=(src, FUNCTIONS), limits=[
            "Sequential event schedules, not a proof of SMP IRQ/tasklet interleavings",
            "Candidate sun4i checks cover a stale IRQ snapshot before the vchan lock and termination from inside a callback; no physical bus timing is emulated",
            "MMIO stop and command-done do not establish physical bus retirement",
            "Null-member-address sanitizer excluded only for vchan_complete; its empty cyclic callback forms an unused null-derived address",
            "Provider descriptor preparation/configuration is checked separately"])
