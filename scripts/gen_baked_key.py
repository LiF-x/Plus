#!/usr/bin/env python3
"""
Generate the obfuscated baked-key header for the LFXE client decrypt path.

Reads a 32-byte key (from a file or generated fresh) and emits
source/client/dts_crypto/lfxe_key_data.h, where the key is stored XOR'd
against a keystream derived from a small seed constant via a fixed LCG.
The full key never appears as a contiguous high-entropy blob in the
binary -- only the seed and the masked bytes do. The same LCG runs in
BakedKeyProvider::GetKey to recover the key at runtime.

This is OBFUSCATION, not protection: anyone reading this source recovers
the scheme trivially. Its only job is to defeat a naive `strings`/entropy
scan of the shipped DLL -- the honest, achievable bar for v1.

Usage:
  # generate a fresh random key, write key + header
  python3 scripts/gen_baked_key.py --new --key-out config/dts_key.bin

  # use an existing key file (the packer reads the same file)
  python3 scripts/gen_baked_key.py --key-in config/dts_key.bin

The emitted header path is fixed (next to the provider source).
"""
import argparse
import os
import sys

HEADER_OUT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "source", "core", "crypto", "lfxe_key_data.h",
)

# Fixed LCG (Numerical Recipes constants). MUST match baked_key_provider.cpp.
LCG_A = 1664525
LCG_C = 1013904223
LCG_MOD = 1 << 32


def lcg_stream(seed: int, n: int) -> bytes:
    out = bytearray()
    state = seed & 0xFFFFFFFF
    for _ in range(n):
        state = (state * LCG_A + LCG_C) % LCG_MOD
        out.append((state >> 24) & 0xFF)
    return bytes(out)


def emit_header(key: bytes, seed: int) -> str:
    assert len(key) == 32
    mask = lcg_stream(seed, 32)
    obf = bytes(k ^ m for k, m in zip(key, mask))
    obf_rows = ",\n\t\t".join(
        ", ".join(f"0x{b:02x}" for b in obf[i:i + 8]) for i in range(0, 32, 8)
    )
    return f"""#pragma once

/* ===================================================================================
\tGENERATED FILE -- do not edit by hand.
\tProduced by scripts/gen_baked_key.py from the project DTS key.

\tThe baked key XOR'd against a fixed-LCG keystream seeded by LFXE_KEY_SEED.
\tBakedKeyProvider::GetKey re-derives the keystream and recovers the key.
\tObfuscation only -- see key_provider.h for the threat-model caveat.
*  =================================================================================== */

#include <cstdint>

namespace lfxe
{{
\tconstexpr uint32_t LFXE_KEY_SEED = 0x{seed:08x}u;

\tconstexpr uint8_t LFXE_KEY_OBF[32] = {{
\t\t{obf_rows}
\t}};
}}
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--new", action="store_true", help="generate a fresh random key")
    ap.add_argument("--key-in", help="read 32-byte key from this file")
    ap.add_argument("--key-out", help="write the key to this file (with --new)")
    ap.add_argument("--seed", help="LCG seed (hex or int); default derived from key")
    args = ap.parse_args()

    if args.new:
        key = os.urandom(32)
        if args.key_out:
            os.makedirs(os.path.dirname(os.path.abspath(args.key_out)), exist_ok=True)
            with open(args.key_out, "wb") as f:
                f.write(key)
            os.chmod(args.key_out, 0o600)
            print(f"wrote key -> {args.key_out}")
    elif args.key_in:
        with open(args.key_in, "rb") as f:
            key = f.read()
        if len(key) != 32:
            print(f"error: key must be 32 bytes, got {len(key)}", file=sys.stderr)
            return 2
    else:
        print("error: pass --new or --key-in", file=sys.stderr)
        return 2

    if args.seed is not None:
        seed = int(args.seed, 0)
    else:
        # Deterministic but key-dependent seed so it isn't a fixed constant
        # across projects; folds the key with a 32-bit FNV-1a.
        h = 0x811C9DC5
        for b in key:
            h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
        seed = h

    with open(HEADER_OUT, "w") as f:
        f.write(emit_header(key, seed))
    print(f"wrote header -> {HEADER_OUT} (seed=0x{seed:08x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
