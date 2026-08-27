/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	BakedKeyProvider: recovers the compiled-in key for keyId 0 by re-deriving
	the LCG keystream (seeded by LFXE_KEY_SEED) and XOR-ing it against the
	obfuscated bytes in the generated lfxe_key_data.h. The LCG constants and
	the >>24 byte tap MUST match scripts/gen_baked_key.py.
*  =================================================================================== */

#include "key_provider.h"
#include "lfxe_key_data.h"

namespace
{
	// Fixed LCG (Numerical Recipes). Keep in lockstep with gen_baked_key.py.
	constexpr uint32_t kLcgA = 1664525u;
	constexpr uint32_t kLcgC = 1013904223u;
}

namespace lfxe
{
	bool BakedKeyProvider::GetKey(uint16_t keyId, uint8_t out[kKeySize])
	{
		if (keyId != 0)
			return false; // baked provider only owns keyId 0

		uint32_t state = LFXE_KEY_SEED;
		for (size_t i = 0; i < kKeySize; ++i)
		{
			state = state * kLcgA + kLcgC; // wraps mod 2^32 by type
			uint8_t mask = (uint8_t)(state >> 24);
			out[i] = (uint8_t)(LFXE_KEY_OBF[i] ^ mask);
		}
		return true;
	}
}
