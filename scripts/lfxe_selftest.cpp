/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	Host-side self-test for the LFXE crypto core. Compiled and run on Linux
	by scripts/test_lfxe_roundtrip.sh -- it exercises the exact same source
	files both DLLs ship (chacha20, lfxe_format, lfxe_decrypt,
	baked_key_provider), so a green run here means the shipped decrypt path
	is byte-correct against the Python packer and the RFC 8439 vector.

	Modes:
	  kat                          -> print 64-byte keystream for the RFC
	                                  8439 §2.3.2 vector (key 00..1f, nonce
	                                  00..09..4a..00, counter 1) as hex.
	  decrypt <in> <keyhex> <out>  -> TryDecryptAsset(in) with keyId-0 key
	                                  = keyhex; write plaintext to <out>.
	  bakedkey                     -> print BakedKeyProvider keyId-0 as hex.
*  =================================================================================== */

#include "../source/core/crypto/chacha20.h"
#include "../source/core/crypto/lfxe_decrypt.h"
#include "../source/core/crypto/key_provider.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	std::vector<uint8_t> readFile(const char* path)
	{
		std::vector<uint8_t> v;
		FILE* f = std::fopen(path, "rb");
		if (!f) return v;
		std::fseek(f, 0, SEEK_END);
		long n = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		if (n > 0) { v.resize((size_t)n); if (std::fread(v.data(), 1, (size_t)n, f) != (size_t)n) v.clear(); }
		std::fclose(f);
		return v;
	}

	bool writeFile(const char* path, const uint8_t* data, size_t len)
	{
		FILE* f = std::fopen(path, "wb");
		if (!f) return false;
		bool ok = len == 0 || std::fwrite(data, 1, len, f) == len;
		std::fclose(f);
		return ok;
	}

	void printHex(const uint8_t* p, size_t n)
	{
		for (size_t i = 0; i < n; ++i) std::printf("%02x", p[i]);
		std::printf("\n");
	}

	bool parseHex(const char* s, std::vector<uint8_t>& out)
	{
		std::string h(s);
		if (h.size() % 2 != 0) return false;
		out.clear();
		for (size_t i = 0; i < h.size(); i += 2)
		{
			auto nyb = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			int hi = nyb(h[i]), lo = nyb(h[i + 1]);
			if (hi < 0 || lo < 0) return false;
			out.push_back((uint8_t)((hi << 4) | lo));
		}
		return true;
	}

	// Fixed-key provider for the round-trip test (keyId 0 only).
	class FixedKeyProvider : public lfxe::KeyProvider
	{
	public:
		explicit FixedKeyProvider(const std::vector<uint8_t>& k) { std::memcpy(key_, k.data(), lfxe::kKeySize); }
		bool GetKey(uint16_t keyId, uint8_t out[lfxe::kKeySize]) override
		{
			if (keyId != 0) return false;
			std::memcpy(out, key_, lfxe::kKeySize);
			return true;
		}
	private:
		uint8_t key_[lfxe::kKeySize];
	};
}

int main(int argc, char** argv)
{
	if (argc < 2) { std::fprintf(stderr, "usage: %s <mode> ...\n", argv[0]); return 2; }
	std::string mode = argv[1];

	if (mode == "kat")
	{
		uint8_t key[32];
		for (int i = 0; i < 32; ++i) key[i] = (uint8_t)i;
		// RFC 8439 §2.3.2 nonce = 00 00 00 09 : 00 00 00 4a : 00 00 00 00
		const uint8_t rfcNonce[12] = {0x00,0x00,0x00,0x09, 0x00,0x00,0x00,0x4a, 0x00,0x00,0x00,0x00};
		uint8_t zero[64] = {0};
		uint8_t ks[64];
		lfxe::chacha20_xor(key, rfcNonce, 1, zero, ks, 64);
		printHex(ks, 64);
		return 0;
	}

	if (mode == "bakedkey")
	{
		lfxe::BakedKeyProvider bp;
		uint8_t k[lfxe::kKeySize];
		if (!bp.GetKey(0, k)) { std::fprintf(stderr, "bakedkey: GetKey failed\n"); return 1; }
		printHex(k, lfxe::kKeySize);
		return 0;
	}

	if (mode == "decrypt" && argc == 5)
	{
		std::vector<uint8_t> in = readFile(argv[2]);
		if (in.empty()) { std::fprintf(stderr, "decrypt: cannot read %s\n", argv[2]); return 1; }
		std::vector<uint8_t> key;
		if (!parseHex(argv[3], key) || key.size() != lfxe::kKeySize) { std::fprintf(stderr, "decrypt: bad keyhex\n"); return 1; }
		FixedKeyProvider kp(key);
		std::vector<uint8_t> plain;
		lfxe::DecryptResult r = lfxe::TryDecryptAsset(in.data(), in.size(), kp, plain);
		if (r != lfxe::DecryptResult::Ok) { std::fprintf(stderr, "decrypt: result=%d\n", (int)r); return 1; }
		if (!writeFile(argv[4], plain.data(), plain.size())) { std::fprintf(stderr, "decrypt: cannot write %s\n", argv[4]); return 1; }
		return 0;
	}

	std::fprintf(stderr, "unknown mode/args\n");
	return 2;
}
