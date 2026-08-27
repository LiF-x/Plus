#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	On-disk container for an encrypted LiFx asset ("LFXE"). A packed DTS is
	just: [LfxeHeader][ChaCha20 ciphertext of the original file bytes].

	Layout (24-byte header, little-endian, no padding):

	    off size field
	    0   4    magic   = 'L','F','X','E'
	    4   1    format  = LFXE_FORMAT_V1
	    5   1    cipher  = LFXE_CIPHER_CHACHA20
	    6   2    keyId   = which key decrypts this (0 = baked key)
	    8   12   nonce   = 96-bit ChaCha20 nonce (unique per file)
	    20  4    reserved= 0
	    24  ...  ciphertext (length implicit: fileSize - 24)

	`keyId` is the door for server-delivered keys: a future ServerKeyProvider
	can hand out per-session keys under non-zero ids without any change to
	this format or to the decrypt hook. The plaintext length is implicit
	because ChaCha20 is a stream cipher (ciphertext length == plaintext
	length), so there is nothing to pad and no length field to forge.

	This header MUST stay byte-compatible with scripts/dts_lib.py (the
	packer). Any change here is a coordinated change there.
*  =================================================================================== */

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lfxe
{
	constexpr uint8_t  kMagic[4]        = {'L', 'F', 'X', 'E'};
	constexpr uint8_t  kFormatV1        = 1;
	constexpr uint8_t  kCipherChaCha20  = 1;
	constexpr size_t   kHeaderSize      = 24;
	constexpr size_t   kNonceSize       = 12;
	constexpr size_t   kKeySize         = 32;

	struct Header
	{
		uint8_t  magic[4];
		uint8_t  format;
		uint8_t  cipher;
		uint16_t keyId;
		uint8_t  nonce[12];
		uint32_t reserved;
	};

	// Returns true and fills `out` if `buf`/`len` begins with a valid,
	// supported LFXE header. Does not validate the ciphertext (a stream
	// cipher has no integrity check by itself -- a wrong key yields
	// garbage, caught downstream by the DTS version gate).
	inline bool ParseHeader(const uint8_t* buf, size_t len, Header& out)
	{
		if (len < kHeaderSize) return false;
		if (std::memcmp(buf, kMagic, 4) != 0) return false;

		out.format = buf[4];
		out.cipher = buf[5];
		out.keyId  = (uint16_t)(buf[6] | (buf[7] << 8));
		std::memcpy(out.nonce, buf + 8, kNonceSize);
		out.reserved = 0;
		std::memcpy(out.magic, buf, 4);

		if (out.format != kFormatV1) return false;
		if (out.cipher != kCipherChaCha20) return false;
		return true;
	}

	// Convenience: does this buffer look like an LFXE container at all?
	inline bool HasMagic(const uint8_t* buf, size_t len)
	{
		return len >= 4 && std::memcmp(buf, kMagic, 4) == 0;
	}
}
