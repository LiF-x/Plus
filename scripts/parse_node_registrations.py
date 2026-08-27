#!/usr/bin/env python3
"""
Parse the four built-in AI behavior-node registration modules out of decompiled
LiF server code and produce the canonical XML-class-name -> C++-class table.

The four module init functions are decompiled by scripts/ghidra/LifxAllRegs.java
into one .c file each. This script reads those decompiles and extracts every
call of the form:

    registerNode(factory, "XmlClassName", &prototypePtr);
or
    registerNode(factory, &DAT_<rva>, &prototypePtr);

For DAT_<rva> name arguments it resolves the string by reading the .rdata
section of the server exe directly (no external Python deps).

Usage:
    parse_node_registrations.py [--exe PATH] [--decompile-dir PATH] [--tsv]

Inputs (defaults match the dev setup in docs/):
    --exe            /home/mjoed/LifeIsFeudal/lif_server_320850/ddctd_cm_yo_server.exe
    --decompile-dir  /tmp/lifx_ghidra/decompile

Default output is a human table; use --tsv for tab-separated machine output.

Background and findings: docs/ai_and_spawning.md §3.4.
"""

import argparse
import os
import re
import struct
import sys
from pathlib import Path

# Each module's init function (vtable slot 1 of its _ModuleInst) at the listed
# RVA. The Ghidra companion script saves the decompile to
# `reg_full_<RVA>.c` in --decompile-dir.
MODULES = {
    "1513B0": "CommonBehaviorNodes",
    "18E8A0": "AnimalBehaviorNodes",
    "2E5FC0": "NPCBehaviorNodes",
    "3E9210": "HorseBehaviorNodes",
}

VTABLE_RE  = re.compile(r"AI::Nodes::([A-Za-z0-9_]+)::vftable")
REG_LIT_RE = re.compile(r'FUN_140153950\s*\(\s*[^,]+,\s*"([^"]+)"')
REG_DAT_RE = re.compile(r"FUN_140153950\s*\(\s*[^,]+,\s*&DAT_([0-9a-fA-F]+)")

# Calls used during normal prototype construction. Seeing these between a vtable
# assignment and a registerNode call does NOT invalidate the vtable-tracking —
# they're framework infrastructure, not prototype mutation.
_NORMAL_CTOR_CALLS = {
    "FUN_140153860",   # getNodeFactory()
    "FUN_140153950",   # registerNode itself
    "FUN_1404551d0",   # INode::reserve_internal_buffer
    "operator_new",
    "memset",
    "FUN_1404555a0",   # std::string c_str-ish helper
    "FUN_140454fa0",   # std::string assign helper
    "FUN_140086d60",   # std::string destroy
    "FUN_140458130",   # tokenizer (used by some value parsers, but irrelevant here)
    "FUN_140455320",   # std::string ctor
    "FUN_140457740",   # string hash
    "FUN_140457190",   # string compare
}
# Any other FUN_<hex>(prototype, …) call is a helper that builds a different
# prototype. After such a helper, the previously seen vtable no longer applies
# to the next register call.
FUN_CALL_RE = re.compile(r"\b(FUN_[0-9a-fA-F]+)\b\s*\(")


class PE:
    """Minimal PE reader: resolve a VA inside .rdata to its C-string content."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        d = self.data
        e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        coff = e_lfanew + 4
        n_sections = struct.unpack_from("<H", d, coff + 2)[0]
        opt_hdr_size = struct.unpack_from("<H", d, coff + 16)[0]
        opt_hdr_off = coff + 20
        # ImageBase for PE32+ is at offset 24 within the OptionalHeader (after Magic).
        magic = struct.unpack_from("<H", d, opt_hdr_off)[0]
        if magic == 0x20B:  # PE32+
            self.image_base = struct.unpack_from("<Q", d, opt_hdr_off + 24)[0]
        else:
            self.image_base = struct.unpack_from("<I", d, opt_hdr_off + 28)[0]
        sec_off = opt_hdr_off + opt_hdr_size
        self.sections = []
        for i in range(n_sections):
            so = sec_off + i * 40
            name = d[so:so + 8].rstrip(b"\0").decode(errors="replace")
            vsize = struct.unpack_from("<I", d, so + 8)[0]
            vaddr = struct.unpack_from("<I", d, so + 12)[0]
            raw_off = struct.unpack_from("<I", d, so + 20)[0]
            self.sections.append((name, vaddr, vsize, raw_off))

    def read_cstring(self, va: int, max_len: int = 256) -> str:
        rva = va - self.image_base
        for _name, vaddr, vsize, raw_off in self.sections:
            if vaddr <= rva < vaddr + vsize:
                fo = raw_off + (rva - vaddr)
                end = self.data.find(b"\x00", fo, fo + max_len)
                if end < 0:
                    end = fo + max_len
                return self.data[fo:end].decode("utf-8", errors="replace")
        raise ValueError(f"VA 0x{va:X} not inside any section")


def parse_module(decompile_path: Path, pe: PE):
    """Yield (xml_name, cpp_class, source_kind) for each register call.

    Tracks the most recent `AI::Nodes::X::vftable` assignment. When a helper
    function call (anything in FUN_<hex>( form that isn't a known boilerplate
    call) appears AFTER that assignment and BEFORE the registerNode, we assume
    the helper has produced a different prototype and emit the helper's RVA in
    place of the C++ class name. RTTI can then tell you what class the helper
    actually constructs (e.g. FUN_140190FB0 -> ChaseEnemyNoAnimation,
    FUN_140191040 -> ClearEnemyInteractions).
    """
    last_vtable = None
    helper_since_vtable = None
    text = decompile_path.read_text()
    for line in text.splitlines():
        m = VTABLE_RE.search(line)
        if m:
            last_vtable = m.group(1)
            helper_since_vtable = None
            continue

        # Inspect any FUN_<hex>( call on this line that isn't a registerNode
        # itself — if it's not a known-boilerplate ctor helper, it's a
        # custom prototype-builder.
        for fcall in FUN_CALL_RE.findall(line):
            if fcall in _NORMAL_CTOR_CALLS:
                continue
            if "FUN_140153950" in line:  # the register call itself, handled below
                continue
            helper_since_vtable = fcall

        m = REG_LIT_RE.search(line)
        if m:
            cls = f"helper({helper_since_vtable})" if helper_since_vtable else last_vtable
            yield m.group(1), cls, "literal"
            helper_since_vtable = None
            continue
        m = REG_DAT_RE.search(line)
        if m:
            va = int(m.group(1), 16)
            try:
                name = pe.read_cstring(va)
            except ValueError as e:
                name = f"<unresolved 0x{va:X}: {e}>"
            cls = f"helper({helper_since_vtable})" if helper_since_vtable else last_vtable
            yield name, cls, f"DAT(0x{va:X})"
            helper_since_vtable = None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default="/home/mjoed/LifeIsFeudal/lif_server_320850/ddctd_cm_yo_server.exe",
                    type=Path, help="path to ddctd_cm_yo_server.exe")
    ap.add_argument("--decompile-dir", default="/tmp/lifx_ghidra/decompile",
                    type=Path, help="dir holding reg_full_<RVA>.c outputs from LifxAllRegs.java")
    ap.add_argument("--tsv", action="store_true",
                    help="emit tab-separated rows instead of the human-readable table")
    args = ap.parse_args()

    if not args.exe.is_file():
        sys.exit(f"exe not found: {args.exe}")
    pe = PE(args.exe)

    all_rows = []
    aliases = []
    classes_seen = set()
    per_module_count = {}
    for rva, mod in MODULES.items():
        path = args.decompile_dir / f"reg_full_{rva}.c"
        if not path.is_file():
            print(f"# WARNING: missing {path} — run scripts/ghidra/LifxAllRegs.java first",
                  file=sys.stderr)
            continue
        n = 0
        for name, cls, src in parse_module(path, pe):
            all_rows.append((mod, name, cls, src))
            classes_seen.add(cls)
            if cls is not None and cls != name:
                aliases.append((mod, name, cls))
            n += 1
        per_module_count[mod] = n

    if args.tsv:
        print("module\txml_name\tcpp_class\tsource")
        for mod, name, cls, src in all_rows:
            print(f"{mod}\t{name}\t{cls or ''}\t{src}")
        return

    # Human-readable
    print(f"{'MODULE':22} {'XML_NAME':30} {'CPP_CLASS':30} SOURCE")
    print("-" * 96)
    for mod, name, cls, src in all_rows:
        print(f"{mod:22} {name:30} {(cls or '?'):30} {src}")
    print()
    print("==== Per-module totals ====")
    total = 0
    for m, c in per_module_count.items():
        print(f"  {m}: {c}")
        total += c
    print(f"  TOTAL: {total}")
    print(f"  Distinct C++ classes: {len(classes_seen)}")
    if aliases:
        print(f"  Aliases (XML name != C++ class, {len(aliases)} entries):")
        for mod, name, cls in aliases:
            print(f"    [{mod}] {name}  ->  {cls}")


if __name__ == "__main__":
    main()
