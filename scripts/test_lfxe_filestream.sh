#!/usr/bin/env bash
# ===================================================================================
#  FileStream-layer LFXE serve self-test (issue #116). Compiles the shared
#  decrypt/serve core (source/core/crypto/lfxe_filestream.cpp) natively and
#  drives it through a mock FileStream to prove that an LFXE container packed
#  under the baked key is served back as byte-exact plaintext for full reads,
#  arbitrary unaligned seeks, and EOF edges -- and that vanilla files and
#  unregistered streams pass straight through. Complements the crypto-only
#  scripts/test_lfxe_roundtrip.sh.
#  Run from the repo root:  bash scripts/test_lfxe_filestream.sh
# ===================================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CXX="${CXX:-g++}"
PY="${PYTHON:-python3}"

echo ">>> generating throwaway key + obfuscated header"
"$PY" scripts/gen_baked_key.py --new --key-out "$TMP/key.bin" >/dev/null

echo ">>> building filestream selftest ($CXX)"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wno-attributes -o "$TMP/fs_selftest" \
  source/core/crypto/chacha20.cpp \
  source/core/crypto/lfxe_decrypt.cpp \
  source/core/crypto/lfxe_filestream.cpp \
  source/core/crypto/baked_key_provider.cpp \
  scripts/lfxe_fs_selftest.cpp

# Pack several plaintext sizes (incl. empty, sub-block, and large >8 KB so the
# real engine would have refilled its buffer window multiple times).
for SIZE in 0 1 64 200 4096 9001 250017; do
  head -c "$SIZE" /dev/urandom > "$TMP/plain.bin" || true
  "$PY" - "$TMP/key.bin" "$TMP/plain.bin" "$TMP/enc.bin" <<'PY'
import sys; sys.path.insert(0, "scripts")
import dts_lib as L
key = open(sys.argv[1], "rb").read()
plain = open(sys.argv[2], "rb").read()
open(sys.argv[3], "wb").write(L.pack(plain, key, key_id=0))
PY
  echo ">>> size=$SIZE"
  "$TMP/fs_selftest" "$TMP/enc.bin" "$TMP/plain.bin"
done

echo "ALL FILESTREAM SELF-TESTS PASSED"
