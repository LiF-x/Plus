#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	Key supply for LFXE decryption. The decrypt path NEVER hard-codes a key
	-- it asks a KeyProvider for the key matching the container's keyId.
	This indirection is the planned migration path to server-delivered keys:

	  * v1 (now): BakedKeyProvider answers keyId 0 with a compiled-in key
	    (de-obfuscated at runtime). Honest, achievable bar -- stops casual
	    ripping, not a determined attacker holding the client DLL.

	  * v2 (later): a ServerKeyProvider answers non-zero keyIds with
	    per-session keys fetched over the existing net channel. Drops in
	    behind this same interface; the LFXE format and the decrypt hook
	    do not change.

	A composite provider can chain both (baked for keyId 0, server for the
	rest), so encrypted-with-baked and encrypted-with-server assets coexist.
*  =================================================================================== */

#include <cstdint>

#include "lfxe_format.h"

namespace lfxe
{
	class KeyProvider
	{
	public:
		virtual ~KeyProvider() = default;

		// Fill out[32] with the key for `keyId`. Return false if this
		// provider does not know that id (the caller then fails the load
		// safely rather than decrypting with a wrong key).
		virtual bool GetKey(uint16_t keyId, uint8_t out[kKeySize]) = 0;
	};

	// Answers keyId 0 from a compiled-in, lightly-obfuscated key. The key
	// bytes are XOR-masked in the binary (see lfxe_key_data.h, generated
	// from config/dts_key.bin by scripts/gen_baked_key.py) so a raw memory
	// scan for a 32-byte high-entropy run does not trivially surface them.
	// This is obfuscation, not protection -- documented as such.
	class BakedKeyProvider : public KeyProvider
	{
	public:
		bool GetKey(uint16_t keyId, uint8_t out[kKeySize]) override;
	};
}
