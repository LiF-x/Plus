"""Read-only verification of the reference client/server native anchors.

Copyright (c) 2026, LiFx Contributors. All Rights Reserved.
Prepared with ChatGPT (Codex), in collaboration with Zbigi.
This file is a part of LiFx.

LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
DEALINGS IN LIFX.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct


def verify(path, identity, sites):
    data = path.read_bytes()
    if (len(data) != identity["size_bytes"]
            or hashlib.sha256(data).hexdigest().upper() != identity["sha256"]):
        raise ValueError("reference binary identity mismatch; use the original supported image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE header")
    if (struct.unpack_from("<H", data, pe + 4)[0] != 0x8664
            or struct.unpack_from("<H", data, pe + 24)[0] != 0x20B):
        raise ValueError("expected AMD64 PE32+")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    table = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    sections = [struct.unpack_from("<IIII", data, table + i * 40 + 8)
                for i in range(count)]
    for site in sites:
        rva = int(site["rva"], 16)
        expected = bytes.fromhex(site["bytes"])
        matches = [raw + rva - va for _, va, size, raw in sections
                   if va <= rva and rva + len(expected) <= va + size]
        if len(matches) != 1:
            raise ValueError(f"{site['name']}: no unique file-backed RVA range")
        offset = matches[0]
        if offset != int(site["file_offset"], 16):
            raise ValueError(f"{site['name']}: file offset mismatch")
        if data[offset:offset + len(expected)] != expected:
            raise ValueError(f"{site['name']}: instruction bytes mismatch")
    return len(sites)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--client", type=Path, required=True,
                        help="original unmodified reference client")
    args = parser.parse_args()
    record = Path(__file__).resolve().parents[1] / "docs/evidence/tree_felling_reference.json"
    try:
        evidence = json.loads(record.read_text(encoding="utf-8"))
        for side in ("server", "client"):
            count = verify(getattr(args, side), evidence["binaries"][side],
                           evidence[f"{side}_sites"])
            print(f"PASS: {side} reference identity and {count} native anchors")
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f"FAIL: {error}\n")
    print("Read-only byte verification; no gameplay or installed-hook acceptance.")


if __name__ == "__main__":
    main()
