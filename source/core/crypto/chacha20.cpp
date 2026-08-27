/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	RFC 8439 ChaCha20. See chacha20.h. Constant-time-ish (no data-dependent
	branches), but note the threat model: this is asset obfuscation, not a
	secret-key guarantee -- a side-channel is irrelevant when the key ships
	in the client. Correctness, not hardening, is the goal here.
*  =================================================================================== */

#include "chacha20.h"

#include <cstring>

namespace
{
	inline uint32_t rotl32(uint32_t x, int n)
	{
		return (x << n) | (x >> (32 - n));
	}

	inline uint32_t load_le32(const uint8_t* p)
	{
		return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	}

	inline void store_le32(uint8_t* p, uint32_t v)
	{
		p[0] = (uint8_t)(v);
		p[1] = (uint8_t)(v >> 8);
		p[2] = (uint8_t)(v >> 16);
		p[3] = (uint8_t)(v >> 24);
	}

#define LFXE_QR(a, b, c, d)               \
	a += b; d ^= a; d = rotl32(d, 16);    \
	c += d; b ^= c; b = rotl32(b, 12);    \
	a += b; d ^= a; d = rotl32(d, 8);     \
	c += d; b ^= c; b = rotl32(b, 7)

	void chacha20_block(const uint32_t in[16], uint8_t out[64])
	{
		uint32_t x[16];
		std::memcpy(x, in, sizeof(x));

		for (int i = 0; i < 10; ++i) // 20 rounds = 10 column + 10 diagonal
		{
			// column rounds
			LFXE_QR(x[0], x[4], x[8], x[12]);
			LFXE_QR(x[1], x[5], x[9], x[13]);
			LFXE_QR(x[2], x[6], x[10], x[14]);
			LFXE_QR(x[3], x[7], x[11], x[15]);
			// diagonal rounds
			LFXE_QR(x[0], x[5], x[10], x[15]);
			LFXE_QR(x[1], x[6], x[11], x[12]);
			LFXE_QR(x[2], x[7], x[8], x[13]);
			LFXE_QR(x[3], x[4], x[9], x[14]);
		}

		for (int i = 0; i < 16; ++i)
			store_le32(out + i * 4, x[i] + in[i]);
	}

#undef LFXE_QR
}

namespace lfxe
{
	void chacha20_xor(const uint8_t key[32],
	                  const uint8_t nonce[12],
	                  uint32_t counter,
	                  const uint8_t* in,
	                  uint8_t* out,
	                  size_t len)
	{
		uint32_t state[16];
		// "expand 32-byte k"
		state[0] = 0x61707865;
		state[1] = 0x3320646e;
		state[2] = 0x79622d32;
		state[3] = 0x6b206574;
		for (int i = 0; i < 8; ++i)
			state[4 + i] = load_le32(key + i * 4);
		state[12] = counter;
		for (int i = 0; i < 3; ++i)
			state[13 + i] = load_le32(nonce + i * 4);

		uint8_t block[64];
		size_t off = 0;
		while (off < len)
		{
			chacha20_block(state, block);
			size_t n = len - off;
			if (n > 64) n = 64;
			for (size_t i = 0; i < n; ++i)
				out[off + i] = in[off + i] ^ block[i];
			off += n;
			++state[12]; // next block counter (32-bit; wraps per RFC)
		}
	}
}
