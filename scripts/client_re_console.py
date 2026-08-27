#!/usr/bin/env python3
"""
Recover client engine console RVAs by anchor-string xref scan.

Each of the four Con::* anchor strings ("Con::sgConsumersMutex",
"Con::init", "Con::InternalConsolePrintf", "Con::evaluate") has exactly
one LEA xref in .text. The containing function (looked up via the
.pdata RUNTIME_FUNCTION table) is the C++ implementation we want to
hook. Same approach used on the server side; verified by inspecting
function prologues:
  - Con::InternalConsolePrintf saves ecx->r15d (matches `(U32 type,...)`)
  - Con::evaluate saves rcx/dl/r8 (matches `(str, echo, fileName)`)
  - Con::init sets four boot flags via `mov byte [rip+...], 1`

Usage:
  python3 scripts/client_re_console.py [path/to/yo_cm_client.exe]

Verified against the qt515-spike client build (24,607,688 bytes,
mtime 2024-05-11). If the binary changes, the .text/.rdata/.pdata
section offsets below need updating from `objdump -h`.
"""
import struct
import sys

DEFAULT_EXE = "extra/lif_client_qt515_spike/yo_cm_client.exe"

# Section layout for the qt515-spike client. Get with `objdump -h <exe>`.
TEXT_FOFF, TEXT_SIZE, TEXT_VA = 0x400,      0x0E893DC, 0x140001000
RDATA_FOFF, RDATA_VMA         = 0xE89800,              0x140E8B000
PDATA_FOFF, PDATA_SIZE        = 0x015DE200, 0x000EC7B4
IMG_BASE                       = 0x140000000

ANCHORS = {
    "Con::sgConsumersMutex":      0xFC2350,
    "Con::init":                  0xFC2E90,
    "Con::InternalConsolePrintf": 0xFC3868,
    "Con::evaluate":              0xFC3900,
}

LEA_PREFIXES = {
    b"\x48\x8d\x05": "rax", b"\x48\x8d\x0d": "rcx", b"\x48\x8d\x15": "rdx",
    b"\x48\x8d\x1d": "rbx", b"\x48\x8d\x35": "rsi", b"\x48\x8d\x3d": "rdi",
    b"\x4c\x8d\x05": "r8",  b"\x4c\x8d\x0d": "r9",  b"\x4c\x8d\x15": "r10",
    b"\x4c\x8d\x1d": "r11", b"\x4c\x8d\x35": "r14", b"\x4c\x8d\x3d": "r15",
}


def main(path: str) -> int:
    with open(path, "rb") as f:
        data = f.read()
    text = data[TEXT_FOFF:TEXT_FOFF + TEXT_SIZE]
    pdata = data[PDATA_FOFF:PDATA_FOFF + PDATA_SIZE]

    def f2va(foff: int) -> int:
        return RDATA_VMA + (foff - RDATA_FOFF)

    targets = {f2va(off): name for name, off in ANCHORS.items()}

    # Find LEA xrefs.
    xrefs = {name: [] for name in ANCHORS}
    for i in range(len(text) - 7):
        reg = LEA_PREFIXES.get(text[i:i + 3])
        if reg is None:
            continue
        disp = struct.unpack_from("<i", text, i + 3)[0]
        instr_va = TEXT_VA + i
        name = targets.get(instr_va + 7 + disp)
        if name is not None:
            xrefs[name].append((instr_va, reg))

    # Build (BeginVA -> EndVA) function ranges from .pdata.
    ranges = []
    for j in range(len(pdata) // 12):
        b, e, _u = struct.unpack_from("<III", pdata, j * 12)
        if b == 0 and e == 0:
            break
        ranges.append((IMG_BASE + b, IMG_BASE + e))
    ranges.sort()

    def fn_of(va: int):
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

    print(f"# binary : {path}")
    print(f"# imgbase: 0x{IMG_BASE:x}")
    for name, xs in xrefs.items():
        for va, reg in xs:
            r = fn_of(va)
            if r is None:
                print(f"{name:30s}  xref@{va:#x} ({reg})  fn=NONE")
                continue
            print(f"{name:30s}  xref@{va:#x} ({reg})  "
                  f"fn=[{r[0]:#x}..{r[1]:#x}]  RVA={r[0] - IMG_BASE:#x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_EXE))
