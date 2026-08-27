#!/usr/bin/env python3
"""
Dump the in-binary process-descriptor table at `DAT_140acfa60` that drives all
LiF furnace behavior (bloomery / kiln / smelter / brewing tank). The table is
59 rows of 28 bytes each, terminated by a row whose first int is 0.

Each row layout (uint32 each):
    +0x00  typeId          hasParent() match anchor — applies the row to any
                           object whose type chain descends from this ID
    +0x04  kind            switch discriminator in WorkingFurnace::recalcTick
                           (1, 2, 3, 4, 5, 6, 7, 8 — see docs/bloomery.md §recalcTick)
    +0x08  factor          IEEE-754 float, typically 1.0f (0x3F800000)
    +0x0C  outputTypeId    transformation target type ID (used for kind 5 and 6)
    +0x10  flag (byte)     non-zero forces Path A (recipe-driven) regardless of kind
    +0x14  field5          consumed by WorkingFurnace::slot 11 when kind==1
    +0x18  tempThreshold   minimum furnace temperature/state to advance

Usage:
    dump_furnace_table.py [--exe PATH] [--rva 0xACFA60]
"""

import argparse
import struct
import sys
from pathlib import Path


def find_file_offset(data: bytes, target_rva: int) -> int:
    e_lfa = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e_lfa + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    oh = struct.unpack_from("<H", data, coff + 16)[0]
    sec_off = coff + 20 + oh
    for i in range(nsec):
        so = sec_off + i * 40
        va = struct.unpack_from("<I", data, so + 12)[0]
        vs = struct.unpack_from("<I", data, so + 8)[0]
        ra = struct.unpack_from("<I", data, so + 20)[0]
        if va <= target_rva < va + vs:
            return ra + (target_rva - va)
    raise ValueError(f"RVA 0x{target_rva:X} not inside any section")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default="/home/mjoed/LifeIsFeudal/lif_server_320850/ddctd_cm_yo_server.exe",
                    type=Path)
    ap.add_argument("--rva", type=lambda s: int(s, 0), default=0xACFA60,
                    help="RVA of the table start (default 0xACFA60)")
    args = ap.parse_args()

    data = args.exe.read_bytes()
    fo = find_file_offset(data, args.rva)
    print(f"# Table at RVA 0x{args.rva:X} -> file offset 0x{fo:X}")
    print(f"# {'#':>2}  {'+0(typeId)':>11}  {'+4(kind)':>9}  "
          f"{'+8(factor)':>12}  {'+0xC(out)':>10}  {'+0x10(flag)':>12}  "
          f"{'+0x14':>6}  {'+0x18(temp)':>12}")
    i = 0
    while i < 1024:  # safety cap
        row = struct.unpack_from("<7I", data, fo + i * 28)
        if row[0] == 0:
            print(f"# --- terminator (row {i}) ---")
            break
        factor = struct.unpack("<f", struct.pack("<I", row[2]))[0] if row[2] else 0.0
        print(f"  {i:>2}  {row[0]:>11}  {row[1]:>9}  "
              f"{factor:>12.4f}  {row[3]:>10}  {row[4]:>12}  "
              f"{row[5]:>6}  {row[6]:>12}")
        i += 1


if __name__ == "__main__":
    main()
