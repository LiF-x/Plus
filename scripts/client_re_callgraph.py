#!/usr/bin/env python3
"""
Reverse call-graph helper for issue #116 M1.

Given a set of target function RVAs, find every direct caller (E8 rel32
`call`, and E9 rel32 tail `jmp`) and map each call site to its containing
function via .pdata. Lets us walk up from the memory-map helpers and from
openFileStream to see whether the streamable-texture path and the
FileStream path share a common engine file-open (design A) or not (B).

Usage:
  python3 scripts/client_re_callgraph.py 0xbfdc80 0xd82630 0x61e620 ...
  python3 scripts/client_re_callgraph.py --callees 0x6c6520   # what 6c6520 calls
"""
import struct
import sys

DEFAULT_EXE = ("/home/mjoed/.local/share/Steam/steamapps/common/"
               "Life is Feudal Your Own/yo_cm_client.exe")
TEXT_FOFF, TEXT_SIZE, TEXT_VA = 0x400, 0x0E893DC, 0x140001000
PDATA_FOFF, PDATA_SIZE = 0x015DE200, 0x000EC7B4
IMG_BASE = 0x140000000


def load():
    with open(DEFAULT_EXE, "rb") as f:
        data = f.read()
    text = data[TEXT_FOFF:TEXT_FOFF + TEXT_SIZE]
    pdata = data[PDATA_FOFF:PDATA_FOFF + PDATA_SIZE]
    ranges = []
    for j in range(len(pdata) // 12):
        b, e, _u = struct.unpack_from("<III", pdata, j * 12)
        if b == 0 and e == 0:
            break
        ranges.append((IMG_BASE + b, IMG_BASE + e))
    ranges.sort()
    return text, ranges


def fn_of(ranges, va):
    lo, hi = 0, len(ranges)
    while lo < hi:
        m = (lo + hi) // 2
        b, e = ranges[m]
        if va < b:
            hi = m
        elif va >= e:
            lo = m + 1
        else:
            return b, e
    return None


def callers_of(text, ranges, target_rvas):
    targets = {IMG_BASE + r for r in target_rvas}
    out = {r: {} for r in target_rvas}   # target -> {callerFnRVA: [siteVAs]}
    for i in range(len(text) - 5):
        op = text[i]
        if op != 0xE8 and op != 0xE9:
            continue
        disp = struct.unpack_from("<i", text, i + 1)[0]
        instr_va = TEXT_VA + i
        tgt = instr_va + 5 + disp
        if tgt not in targets:
            continue
        r = fn_of(ranges, instr_va)
        fn = (r[0] - IMG_BASE) if r else None
        out[tgt - IMG_BASE].setdefault(fn, []).append(instr_va)
    return out


def callees_of(text, ranges, fn_rva):
    r = fn_of(ranges, IMG_BASE + fn_rva)
    if not r:
        return []
    lo, hi = r
    out = {}
    off = lo - TEXT_VA
    end = hi - TEXT_VA
    i = off
    while i < end - 5:
        if text[i] == 0xE8:
            disp = struct.unpack_from("<i", text, i + 1)[0]
            tgt = TEXT_VA + i + 5 + disp
            if TEXT_VA <= tgt < TEXT_VA + TEXT_SIZE:
                tr = fn_of(ranges, tgt)
                key = (tr[0] - IMG_BASE) if tr else (tgt - IMG_BASE)
                out.setdefault(key, []).append(TEXT_VA + i)
        i += 1
    return out


def main(argv):
    text, ranges = load()
    if argv and argv[0] == "--callees":
        for a in argv[1:]:
            fn = int(a, 16)
            print(f"\n== callees of {fn:#x} ==")
            for callee, sites in sorted(callees_of(text, ranges, fn).items()):
                print(f"  -> {callee:#x}   ({len(sites)} call)")
        return 0
    rvas = [int(a, 16) for a in argv]
    res = callers_of(text, ranges, rvas)
    for r in rvas:
        print(f"\n== callers of {r:#x} ==")
        cs = res[r]
        if not cs:
            print("  (no direct rel32 caller -- virtual/indirect only?)")
        for fn, sites in sorted(cs.items(), key=lambda kv: (kv[0] is None, kv[0])):
            fns = f"{fn:#x}" if fn is not None else "NONE"
            print(f"  {fns}  @ {', '.join(hex(s) for s in sites)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
