#!/usr/bin/env bash
# ===================================================================================
#  LFXE crypto self-test. Proves the shipped client decrypt path is byte-correct:
#    1. KAT   : C and Python ChaCha20 both match the RFC 8439 §2.3.2 keystream.
#    2. KEY   : C BakedKeyProvider recovers exactly the key the generator wrote.
#    3. TRIP  : Python packs N random blobs (incl. empty + non-block-multiple),
#               the C decrypt path recovers each one bit-for-bit.
#  Run from the repo root:  bash scripts/test_lfxe_roundtrip.sh
# ===================================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CXX="${CXX:-g++}"
PY="${PYTHON:-python3}"

# RFC 8439 §2.3.2 keystream (key=00..1f, nonce=00 00 00 09 00 00 00 4a 00 00 00 00, counter=1).
RFC_KAT="10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4ed282644607 9faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e"
RFC_KAT="${RFC_KAT// /}"

echo ">>> generating throwaway key + obfuscated header"
"$PY" scripts/gen_baked_key.py --new --key-out "$TMP/key.bin" >/dev/null
KEYHEX="$("$PY" - "$TMP/key.bin" <<'PY'
import sys
sys.stdout.write(open(sys.argv[1],"rb").read().hex())
PY
)"

echo ">>> building selftest ($CXX)"
"$CXX" -std=c++17 -O2 -Wall -Wextra -o "$TMP/selftest" \
  source/core/crypto/chacha20.cpp \
  source/core/crypto/lfxe_decrypt.cpp \
  source/core/crypto/baked_key_provider.cpp \
  scripts/lfxe_selftest.cpp

fail() { echo "FAIL: $1" >&2; exit 1; }

# --- 1. KAT -----------------------------------------------------------------
C_KAT="$("$TMP/selftest" kat)"
PY_KAT="$("$PY" - <<PY
import sys; sys.path.insert(0, "scripts")
import dts_lib as L
key=bytes(range(32))
nonce=bytes([0,0,0,9, 0,0,0,0x4a, 0,0,0,0])
ks=L.chacha20_xor(key, nonce, 1, b"\x00"*64)
print(ks.hex())
PY
)"
[ "$C_KAT" = "$PY_KAT" ]   || fail "ChaCha20 C vs Python keystream differ:\n  C =$C_KAT\n  py=$PY_KAT"
[ "$C_KAT" = "$RFC_KAT" ]  || fail "ChaCha20 keystream != RFC 8439 §2.3.2 vector:\n  got=$C_KAT\n  rfc=$RFC_KAT"
echo "PASS 1/3 KAT  (C == Python == RFC 8439 §2.3.2)"

# --- 2. baked key recovery --------------------------------------------------
C_BAKED="$("$TMP/selftest" bakedkey)"
[ "$C_BAKED" = "$KEYHEX" ] || fail "BakedKeyProvider recovered wrong key:\n  got=$C_BAKED\n  exp=$KEYHEX"
echo "PASS 2/3 KEY  (BakedKeyProvider == generator key)"

# --- 3. round trip ----------------------------------------------------------
for SIZE in 0 1 63 64 65 4096 250017; do
  head -c "$SIZE" /dev/urandom > "$TMP/plain.bin" || true
  "$PY" - "$TMP/key.bin" "$TMP/plain.bin" "$TMP/enc.bin" <<'PY'
import sys; sys.path.insert(0, "scripts")
import dts_lib as L
key = open(sys.argv[1], "rb").read()
plain = open(sys.argv[2], "rb").read()
open(sys.argv[3], "wb").write(L.pack(plain, key, key_id=0))
PY
  "$TMP/selftest" decrypt "$TMP/enc.bin" "$KEYHEX" "$TMP/out.bin"
  cmp -s "$TMP/plain.bin" "$TMP/out.bin" || fail "round trip mismatch at size=$SIZE"
done
echo "PASS 3/3 TRIP (Python pack -> C decrypt, sizes 0..250017)"

echo "ALL LFXE CRYPTO SELF-TESTS PASSED"
