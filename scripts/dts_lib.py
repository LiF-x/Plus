#!/usr/bin/env python3
"""
LFXE pack/unpack + a pure-Python RFC 8439 ChaCha20.

Shared by the packer (scripts/dts_encrypt.py) and the interop test. Pure
Python so the asset pipeline needs no third-party crypto dependency; pack
is an offline, one-time step over a few hundred shape files, so speed is a
non-issue.

The byte layout MUST stay identical to source/client/dts_crypto/
(lfxe_format.h, chacha20.cpp). Both sides are pinned to the RFC 8439
§2.4.2 keystream/test vector by scripts/test_lfxe_roundtrip.sh.
"""
import os
import struct

MAGIC = b"LFXE"
FORMAT_V1 = 1
CIPHER_CHACHA20 = 1
HEADER_SIZE = 24
NONCE_SIZE = 12
KEY_SIZE = 32
PAYLOAD_COUNTER = 1  # RFC 8439: payload starts at block counter 1

_MASK32 = 0xFFFFFFFF


def _rotl32(x, n):
    return ((x << n) | (x >> (32 - n))) & _MASK32


def _quarter(s, a, b, c, d):
    s[a] = (s[a] + s[b]) & _MASK32; s[d] ^= s[a]; s[d] = _rotl32(s[d], 16)
    s[c] = (s[c] + s[d]) & _MASK32; s[b] ^= s[c]; s[b] = _rotl32(s[b], 12)
    s[a] = (s[a] + s[b]) & _MASK32; s[d] ^= s[a]; s[d] = _rotl32(s[d], 8)
    s[c] = (s[c] + s[d]) & _MASK32; s[b] ^= s[c]; s[b] = _rotl32(s[b], 7)


def _chacha20_block(key, counter, nonce):
    const = (0x61707865, 0x3320646e, 0x79622d32, 0x6b206574)
    state = list(const)
    state += list(struct.unpack("<8I", key))
    state.append(counter & _MASK32)
    state += list(struct.unpack("<3I", nonce))
    working = list(state)
    for _ in range(10):
        _quarter(working, 0, 4, 8, 12)
        _quarter(working, 1, 5, 9, 13)
        _quarter(working, 2, 6, 10, 14)
        _quarter(working, 3, 7, 11, 15)
        _quarter(working, 0, 5, 10, 15)
        _quarter(working, 1, 6, 11, 12)
        _quarter(working, 2, 7, 8, 13)
        _quarter(working, 3, 4, 9, 14)
    out = [(working[i] + state[i]) & _MASK32 for i in range(16)]
    return struct.pack("<16I", *out)


def chacha20_xor(key, nonce, counter, data):
    """RFC 8439 ChaCha20. key=32 bytes, nonce=12 bytes."""
    assert len(key) == KEY_SIZE and len(nonce) == NONCE_SIZE
    out = bytearray(len(data))
    off = 0
    blk = counter
    while off < len(data):
        ks = _chacha20_block(key, blk, nonce)
        n = min(64, len(data) - off)
        for i in range(n):
            out[off + i] = data[off + i] ^ ks[i]
        off += n
        blk = (blk + 1) & _MASK32
    return bytes(out)


def pack(plaintext, key, key_id=0, nonce=None):
    """Wrap plaintext bytes in an LFXE container."""
    if nonce is None:
        nonce = os.urandom(NONCE_SIZE)
    assert len(nonce) == NONCE_SIZE
    header = MAGIC + struct.pack("<BBH", FORMAT_V1, CIPHER_CHACHA20, key_id)
    header += nonce + struct.pack("<I", 0)
    assert len(header) == HEADER_SIZE
    ct = chacha20_xor(key, nonce, PAYLOAD_COUNTER, plaintext)
    return header + ct


def has_magic(buf):
    return len(buf) >= 4 and buf[:4] == MAGIC


def unpack(container, key):
    """Inverse of pack(); raises ValueError on a bad/unsupported header."""
    if len(container) < HEADER_SIZE or container[:4] != MAGIC:
        raise ValueError("not an LFXE container")
    fmt, cipher, key_id = struct.unpack_from("<BBH", container, 4)
    if fmt != FORMAT_V1:
        raise ValueError(f"unsupported format {fmt}")
    if cipher != CIPHER_CHACHA20:
        raise ValueError(f"unsupported cipher {cipher}")
    nonce = container[8:20]
    ct = container[HEADER_SIZE:]
    return chacha20_xor(key, nonce, PAYLOAD_COUNTER, ct)
