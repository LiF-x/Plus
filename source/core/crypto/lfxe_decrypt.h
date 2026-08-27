#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	One-call decrypt of an LFXE container. The TSShape::read hook (M3) reads
	the source stream into a buffer and calls TryDecryptAsset; on success it
	hands the plaintext to the original parser, on failure it passes the
	original bytes through unchanged (vanilla DTS) or fails the load safely.
*  =================================================================================== */

#include <cstddef>
#include <cstdint>
#include <vector>

#include "key_provider.h"

namespace lfxe
{
	enum class DecryptResult
	{
		Ok,            // outPlain holds the decrypted bytes
		NotEncrypted,  // no LFXE magic -- caller should pass bytes through
		BadHeader,     // LFXE magic but unsupported/garbled header
		NoKey,         // header ok, but the provider has no key for keyId
	};

	// ChaCha20 payload starts at block counter 1 (RFC 8439 §2.4: counter 0
	// is reserved for the Poly1305 one-time key in AEAD, which we don't use,
	// but we follow the convention so the packer and any future AEAD upgrade
	// stay aligned).
	constexpr uint32_t kPayloadCounter = 1;

	DecryptResult TryDecryptAsset(const uint8_t* in,
	                              size_t inLen,
	                              KeyProvider& keys,
	                              std::vector<uint8_t>& outPlain);
}
