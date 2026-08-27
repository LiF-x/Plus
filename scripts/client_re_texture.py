#!/usr/bin/env python3
"""
M1 reverse-engineering for the universal LFXE decrypt hook (issue #116).

Goal: decide design A (hook one engine file-open that the streamable/
memory-mapped texture path shares with FileStream) vs design B (Win32
catch-all: hook CreateFileMappingW/MapViewOfFile/... directly).

Method (same toolkit as scripts/client_re_dts.py -- pure struct, no
capstone):
  1. Resolve the IAT slot RVAs of the file/mapping Win32 APIs.
  2. Scan .text for `call qword ptr [rip+disp]` (FF 15) and tail-jump
     thunks (FF 25) whose target is one of those slots; record the call
     sites.
  3. Map each call site to its containing function via the .pdata
     RUNTIME_FUNCTION table.
  4. Pull LEA xrefs to texture/PNG/streamable anchor strings and map
     those to functions too.
  5. Report which functions touch CreateFileMapping / MapViewOfFile, and
     whether any of them is (or is called by) the known openFileStream
     factory at RVA 0x61E620 -- the seam the DTS/DSO hooks already use.

Usage:
  python3 scripts/client_re_texture.py [path/to/yo_cm_client.exe]
"""
import struct
import sys

DEFAULT_EXE = ("/home/mjoed/.local/share/Steam/steamapps/common/"
               "Life is Feudal Your Own/yo_cm_client.exe")

# Section layout for the 24,607,688-byte client (objdump -h). Identical
# between the qt515 spike and the live Steam binary.
TEXT_FOFF, TEXT_SIZE, TEXT_VA = 0x400, 0x0E893DC, 0x140001000
RDATA_FOFF, RDATA_SIZE, RDATA_VA = 0xE89800, 0x006A4A4C, 0x140E8B000
PDATA_FOFF, PDATA_SIZE = 0x015DE200, 0x000EC7B4
IMG_BASE = 0x140000000

# Known seam from PR #113/#115 (RVA), for the A-vs-B correlation.
OPEN_FILE_STREAM_RVA = 0x61E620

# IAT slot RVAs (objdump -p import table; the live binary).
IAT = {
    "GetFileSizeEx":       0xE8B1E8,
    "CreateFileMappingW":  0xE8B3D8,
    "MapViewOfFile":       0xE8B3E0,
    "SetFilePointerEx":    0xE8B5F0,
    "ReadFile":            0xE8B610,
    "CreateFileW":         0xE8B618,
    "MapViewOfFileEx":     0xE8B680,
    "UnmapViewOfFile":     0xE8B690,
    "CreateFileA":         0xE8B3D0,
}

# Anchor substrings to locate (case-sensitive bytes) in .rdata. We scan
# the section for each and take LEA xrefs to the match address.
ANCHOR_NEEDLES = [
    b"GFXStreamableTextureManager",
    b"gfxstreamabletexturemanager",
    b"StreamableTexture",
    b"streamPool",
    b"libpng",
    b"Read Error",        # libpng error path / GBitmap png reader
    b"png_",
    b".png",
    b"PNGReadFn",
    b"GBitmap",
    b"DDSFile",
    b"MapViewOfFile",     # any in-engine diagnostic naming it
]

LEA_PREFIXES = {
    b"\x48\x8d\x05": "rax", b"\x48\x8d\x0d": "rcx", b"\x48\x8d\x15": "rdx",
    b"\x48\x8d\x1d": "rbx", b"\x48\x8d\x35": "rsi", b"\x48\x8d\x3d": "rdi",
    b"\x4c\x8d\x05": "r8",  b"\x4c\x8d\x0d": "r9",  b"\x4c\x8d\x15": "r10",
    b"\x4c\x8d\x1d": "r11", b"\x4c\x8d\x35": "r14", b"\x4c\x8d\x3d": "r15",
}


def build_ranges(pdata):
    ranges = []
    for j in range(len(pdata) // 12):
        b, e, _u = struct.unpack_from("<III", pdata, j * 12)
        if b == 0 and e == 0:
            break
        ranges.append((IMG_BASE + b, IMG_BASE + e))
    ranges.sort()
    return ranges


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


def main(path):
    with open(path, "rb") as f:
        data = f.read()
    text = data[TEXT_FOFF:TEXT_FOFF + TEXT_SIZE]
    rdata = data[RDATA_FOFF:RDATA_FOFF + RDATA_SIZE]
    pdata = data[PDATA_FOFF:PDATA_FOFF + PDATA_SIZE]
    ranges = build_ranges(pdata)

    def rva(va):
        return va - IMG_BASE

    # ---- 1+2+3: call/jmp xrefs to the IAT slots --------------------------
    iat_va = {name: IMG_BASE + slot for name, slot in IAT.items()}
    by_target = {va: name for name, va in iat_va.items()}
    # callers[name] = set of containing-function RVAs that call it
    callers = {name: {} for name in IAT}     # fnRVA -> [callsite VAs]

    for i in range(len(text) - 6):
        op = text[i:i + 2]
        if op != b"\xff\x15" and op != b"\xff\x25":
            continue
        disp = struct.unpack_from("<i", text, i + 2)[0]
        instr_va = TEXT_VA + i
        target = instr_va + 6 + disp
        name = by_target.get(target)
        if name is None:
            continue
        r = fn_of(ranges, instr_va)
        fn = rva(r[0]) if r else None
        callers[name].setdefault(fn, []).append(instr_va)

    print(f"# binary : {path}")
    print(f"# openFileStream seam: RVA {OPEN_FILE_STREAM_RVA:#x}\n")
    print("== Win32 file/mapping API call sites (FF15/FF25 -> IAT) ==")
    mapping_fns = set()
    for name in IAT:
        sites = callers[name]
        total = sum(len(v) for v in sites.values())
        print(f"\n{name}: {total} call site(s) in {len(sites)} fn(s)")
        for fn, vas in sorted(sites.items(), key=lambda kv: (kv[0] is None, kv[0])):
            tag = ""
            if fn is not None and fn == OPEN_FILE_STREAM_RVA:
                tag = "  <<< openFileStream"
            if name in ("CreateFileMappingW", "MapViewOfFile", "MapViewOfFileEx",
                        "UnmapViewOfFile") and fn is not None:
                mapping_fns.add(fn)
            fns = f"fn=RVA {fn:#x}" if fn is not None else "fn=NONE"
            print(f"    {fns}  calls@ {', '.join(f'{v:#x}' for v in vas)}{tag}")

    # ---- 4: anchor-string LEA xrefs -> functions -------------------------
    # find needle addresses in .rdata
    needle_addrs = {}   # va -> label
    for needle in ANCHOR_NEEDLES:
        start = 0
        hits = 0
        while True:
            k = rdata.find(needle, start)
            if k < 0:
                break
            va = RDATA_VA + k
            needle_addrs[va] = needle.decode("latin1")
            start = k + 1
            hits += 1
            if hits >= 6:
                break

    # scan text for LEA to any needle addr
    anchor_xref = {}   # label -> list of (fnRVA, callsiteVA)
    for i in range(len(text) - 7):
        if text[i:i + 3] not in LEA_PREFIXES:
            continue
        disp = struct.unpack_from("<i", text, i + 3)[0]
        instr_va = TEXT_VA + i
        tgt = instr_va + 7 + disp
        label = needle_addrs.get(tgt)
        if label is None:
            continue
        r = fn_of(ranges, instr_va)
        fn = rva(r[0]) if r else None
        anchor_xref.setdefault(label, []).append((fn, instr_va))

    print("\n\n== Anchor-string LEA xrefs ==")
    for label in sorted(set(needle_addrs.values())):
        xs = anchor_xref.get(label, [])
        if not xs:
            print(f"\n{label!r}: present in .rdata, no LEA xref in .text")
            continue
        print(f"\n{label!r}: {len(xs)} xref(s)")
        seen = set()
        for fn, va in xs:
            key = (fn, va)
            if key in seen:
                continue
            seen.add(key)
            mark = "  [maps texture]" if fn in mapping_fns else ""
            fns = f"fn=RVA {fn:#x}" if fn is not None else "fn=NONE"
            print(f"    {fns}  lea@ {va:#x}{mark}")

    # ---- 5: does any mapping fn also reach openFileStream? ---------------
    print("\n\n== A-vs-B signal ==")
    print(f"functions that call a MapView/CreateFileMapping API: "
          f"{', '.join(hex(f) for f in sorted(mapping_fns))}")
    if OPEN_FILE_STREAM_RVA in mapping_fns:
        print("  openFileStream itself memory-maps -> design A covers textures.")
    else:
        print("  openFileStream does NOT memory-map; mapped textures take a "
              "separate path -> lean design B (or a second seam).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_EXE))
