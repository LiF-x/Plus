#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	RFC 8439 ChaCha20 stream cipher. Vendored single-file implementation
	(no external crypto dependency in the injected DLL). Used to decrypt
	LFXE-wrapped DTS shapes in-process before the engine's TSShape parser
	sees the bytes. The matching encrypt side lives in scripts/dts_lib.py;
	both are validated against the RFC 8439 §2.4.2 test vector.

	ChaCha20 is a stream cipher: keystream = f(key, nonce, counter), and
	ciphertext = plaintext XOR keystream. This is why the LFXE format needs
	no padding or block alignment, and why a consumer can decrypt from any
	byte offset -- the DTS loader seeks within the shape stream.
*  =================================================================================== */

#include <cstddef>
#include <cstdint>

namespace lfxe
{
	// XOR `len` bytes of `in` with the ChaCha20 keystream derived from
	// (key, nonce, counter), writing to `out`. `in` and `out` may alias.
	// `counter` is the initial 32-bit block counter (RFC 8439 starts the
	// payload at counter=1; counter=0 is reserved for Poly1305 in AEAD,
	// which we do not use). nonce is 96 bits (12 bytes).
	void chacha20_xor(const uint8_t key[32],
	                  const uint8_t nonce[12],
	                  uint32_t counter,
	                  const uint8_t* in,
	                  uint8_t* out,
	                  size_t len);
}
