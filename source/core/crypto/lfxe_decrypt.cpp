/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (shared client/server crypto).
*  =================================================================================== */

#include "lfxe_decrypt.h"

#include "chacha20.h"
#include "lfxe_format.h"

namespace lfxe
{
	DecryptResult TryDecryptAsset(const uint8_t* in,
	                              size_t inLen,
	                              KeyProvider& keys,
	                              std::vector<uint8_t>& outPlain)
	{
		if (!HasMagic(in, inLen))
			return DecryptResult::NotEncrypted;

		Header h;
		if (!ParseHeader(in, inLen, h))
			return DecryptResult::BadHeader;

		uint8_t key[kKeySize];
		if (!keys.GetKey(h.keyId, key))
			return DecryptResult::NoKey;

		const size_t plainLen = inLen - kHeaderSize;
		outPlain.resize(plainLen);
		if (plainLen != 0)
		{
			chacha20_xor(key, h.nonce, kPayloadCounter,
			             in + kHeaderSize, outPlain.data(), plainLen);
		}
		return DecryptResult::Ok;
	}
}
