# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the pinned driver TU used by cross-boundary adapter tests."""
import hashlib
import re
from boundary_common import HERE
from run_c import STRUCTURES, ARRAYS, extract_function, chunk_defines, oob_ops_definitions, driver_functions
from run_c import oob_policy_defines, oob_policy_functions, oob_policy_prototype
from run_core import extract


def fixture_body(text, name):
    match = re.search(r"^static [^;{}]*\b" + name + r"\([^;{}]*\)\s*\{",text,re.M)
    if not match:
        raise ValueError("missing fixture: " + name)
    depth=1
    for end in range(match.end(),len(text)):
        depth += (text[end]=="{") - (text[end]=="}")
        if not depth:
            return text[match.start():end+1]
    raise ValueError("unterminated fixture")


def generate(src, *, persistent=False):
    source = src["sunxi_nand.c"]
    header = (HERE/"tests/fixtures.h").read_text()
    extra = []
    if persistent:
        extra = ["sunxi_nfc_randomizer_config", "sunxi_nfc_randomizer_enable",
                 "sunxi_nfc_randomizer_disable", "sunxi_nfc_randomizer_read_buf",
                 "sunxi_nfc_randomizer_write_buf", "sunxi_nfc_hw_ecc_enable", "sunxi_nfc_hw_ecc_disable"]
        replacements = ["writel", "transfer", "nand_read_page_op", "nand_change_read_column_op",
            "sunxi_nfc_select_chip", "nand_prog_page_begin_op", "nand_change_write_column_op",
            "nand_prog_page_end_op", "sunxi_nfc_dma_op_prepare", *extra]
        # Replace only host fixtures with prototypes; source bodies stay intact.
        for name in replacements:
            body = fixture_body(header,name)
            header = header.replace(body,body[:body.index("{")] + ";",1)
        header = header.replace("unsigned reg_spare_area;", "unsigned reg_spare_area, random_en_mask;")
        header = header.replace("static unsigned raw_calls, fail_raw_at, erased_flips, column, page_reads, column_reads;",
                                "static unsigned erased_flips, column;")
        header = header.replace("static unsigned plain_read_calls, random_read_calls;", "static unsigned plain_read_calls;")
        header = header.replace("static const u8 *format_image;\nstatic size_t format_image_len;", "")
        header += "\n" + (HERE/"tests/persistent_driver.h").read_text()
    generated = [oob_policy_defines(source), header, chunk_defines(source), oob_policy_prototype(source),
                 "struct sunxi_nfc_ecc_read; struct sunxi_nfc_ecc_snapshot;"]
    hashes = {}
    for name in STRUCTURES:
        start = source.index("struct " + name + " {")
        generated.append(source[start:source.index("\n};",start)+3])
    for name in ARRAYS:
        generated.append(re.search(r"^static const u(?:8|16) " + name + r"\[\] = \{.*?\n};",source,re.M|re.S)[0])
    for name in [*driver_functions(source),*extra]:
        if name == "sunxi_nfc_allwinner_oob_attach_chip":
            generated.extend(oob_ops_definitions(source))
        body = extract_function(source,name)
        generated.append(f'#line {source[:source.index(body)].count(chr(10))+1} "sunxi_nand.c"\n'+body)
        hashes["sunxi_nand.c:"+name] = hashlib.sha256(body.encode()).hexdigest()
    helpers = (HERE/"tests/check.c").read_text()
    generated += ['#line 1 "driver-adapter.c"', extract(helpers,"caps"), extract(helpers,"chip"),
                  (HERE/"tests/driver_attach_boundary.c").read_text()]
    if persistent:
        generated.append((HERE/"tests/persistent_driver.c").read_text())
    return "\n".join(generated), hashes
