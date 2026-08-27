/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	Native unit test for the FileStream-layer LFXE serve logic
	(source/core/crypto/lfxe_filestream.cpp, issue #116). The in-engine detour
	can't run off-Wine, but the registry + serve path is ABI-agnostic: we mock
	a FileStream with a small struct (status at +0x08, as the engine lays it
	out) and plain method functions over a ciphertext buffer, then exercise
	FsRegisterIfEncrypted / FsTryRead / FsTryGetSize / FsTrySetPosition /
	FsUnregister exactly as the detours do.

	Usage: lfxe_fs_selftest <container.lfxe> <expected_plaintext>
	(container must be packed under the baked key compiled into this binary.)
*  =================================================================================== */

#include "../source/core/crypto/lfxe_filestream.h"
#include "../source/core/crypto/key_provider.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	// Mock FileStream: the core only touches status at +0x08, so we mirror
	// that offset. The rest is our own bookkeeping for the mock methods.
	struct MockStream
	{
		std::uint64_t vtbl;          // +0x00 (unused)
		std::uint32_t status;        // +0x08 (core writes here)
		std::uint32_t pad;           // +0x0C
		const std::uint8_t* data;    // ciphertext
		std::uint64_t size;
		std::uint64_t pos;
	};

	std::uint64_t mockGetSize(void* self) { return static_cast<MockStream*>(self)->size; }
	bool mockSetPos(void* self, std::uint64_t p)
	{
		auto* s = static_cast<MockStream*>(self);
		if (p > s->size) return false;
		s->pos = p; return true;
	}
	bool mockRead(void* self, std::uint64_t n, void* dst)
	{
		auto* s = static_cast<MockStream*>(self);
		std::uint64_t avail = s->pos <= s->size ? s->size - s->pos : 0;
		std::uint64_t take = n <= avail ? n : avail;
		if (take) std::memcpy(dst, s->data + s->pos, (size_t)take);
		s->pos += take;
		return take == n;
	}

	std::vector<std::uint8_t> readFile(const char* p)
	{
		std::FILE* f = std::fopen(p, "rb");
		if (!f) { std::fprintf(stderr, "cannot open %s\n", p); std::exit(2); }
		std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
		std::vector<std::uint8_t> v(n > 0 ? n : 0);
		if (n > 0 && std::fread(v.data(), 1, n, f) != (size_t)n) { std::exit(2); }
		std::fclose(f);
		return v;
	}

	int failures = 0;
	void check(bool cond, const char* what)
	{
		if (cond) std::printf("  ok   %s\n", what);
		else { std::printf("  FAIL %s\n", what); ++failures; }
	}
}

int main(int argc, char** argv)
{
	if (argc != 3) { std::fprintf(stderr, "usage: %s <container> <plaintext>\n", argv[0]); return 2; }
	std::vector<std::uint8_t> cipher = readFile(argv[1]);
	std::vector<std::uint8_t> plain  = readFile(argv[2]);
	lfxe::BakedKeyProvider keys;

	MockStream ms{0, 0, 0, cipher.data(), cipher.size(), 0};

	// 1. register: an LFXE container under the baked key decrypts.
	bool reg = lfxe::FsRegisterIfEncrypted(&ms, mockGetSize, mockSetPos, mockRead, keys);
	check(reg, "FsRegisterIfEncrypted accepts LFXE container");

	// 2. size reflects the *plaintext*, not the container.
	std::uint64_t sz = 0;
	check(lfxe::FsTryGetSize(&ms, &sz) && sz == plain.size(), "getStreamSize == plaintext size");

	// 3. full sequential read returns the exact plaintext.
	{
		std::uint64_t p = 999; bool sp; lfxe::FsTrySetPosition(&ms, 0, &sp);
		std::vector<std::uint8_t> out(plain.size());
		bool rd; bool handled = lfxe::FsTryRead(&ms, plain.size(), out.data(), &rd);
		check(handled && rd, "full read returns true");
		check(out == plain, "full read bytes == plaintext");
		check(lfxe::FsTryGetPosition(&ms, &p) && p == plain.size(), "position advanced to EOF");
	}

	// 4. random-access seeks decrypt correctly at arbitrary (unaligned) offsets.
	if (!plain.empty())
	{
		const std::uint64_t offs[] = {1, 7, 63, 64, 65, plain.size() / 2, plain.size() - 1};
		bool allMatch = true;
		for (std::uint64_t off : offs)
		{
			if (off >= plain.size()) continue;
			bool sp; lfxe::FsTrySetPosition(&ms, off, &sp);
			std::uint64_t want = plain.size() - off;
			std::uint64_t take = want < 100 ? want : 100;
			std::vector<std::uint8_t> out(take);
			bool rd; lfxe::FsTryRead(&ms, take, out.data(), &rd);
			if (std::memcmp(out.data(), plain.data() + off, (size_t)take) != 0) allMatch = false;
		}
		check(allMatch, "unaligned seek+read bytes match plaintext");
	}

	// 5. seek past end is rejected (matching the engine), in-range accepted.
	{
		bool sp;
		bool h1 = lfxe::FsTrySetPosition(&ms, plain.size() + 1, &sp);
		check(h1 && !sp, "seek past end rejected");
		bool h2 = lfxe::FsTrySetPosition(&ms, plain.size(), &sp);
		check(h2 && sp, "seek to exact end accepted");
	}

	// 6. unregister: the stream is no longer LFXE-backed (detour passthrough).
	lfxe::FsUnregister(&ms);
	{
		std::uint64_t s; bool rd;
		check(!lfxe::FsTryGetSize(&ms, &s), "after unregister getSize passes through");
		check(!lfxe::FsTryRead(&ms, 1, nullptr, &rd), "after unregister read passes through");
	}

	// 7. a non-LFXE buffer is never registered (vanilla files untouched).
	{
		std::vector<std::uint8_t> vanilla = {'D', 'D', 'S', ' ', 1, 2, 3, 4, 5, 6, 7, 8,
		                                     9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
		MockStream v{0, 0, 0, vanilla.data(), vanilla.size(), 0};
		bool r = lfxe::FsRegisterIfEncrypted(&v, mockGetSize, mockSetPos, mockRead, keys);
		check(!r, "non-LFXE buffer not registered");
		bool rd;
		check(!lfxe::FsTryRead(&v, 1, nullptr, &rd), "non-LFXE read passes through");
	}

	std::printf(failures ? "\nFILESTREAM SELF-TEST FAILED (%d)\n" : "\nFILESTREAM SELF-TEST PASSED\n", failures);
	return failures ? 1 : 0;
}
