/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (shared client/server crypto).

	FileStream-layer LFXE decrypt registry. See lfxe_filestream.h.
*  =================================================================================== */

#include "lfxe_filestream.h"

#include "lfxe_decrypt.h"
#include "lfxe_format.h"

#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>

// Lock primitive. On Windows we deliberately use a Win32 SRWLOCK from
// KERNEL32 rather than std::mutex: std::mutex::lock() calls out to MSVCP140's
// _Mtx_* exports, and in the *client* process's Wine/CRT context that path
// faults (the server, which ships its own native CRT DLLs, is unaffected) --
// this was the #116 client boot crash, pinpointed to the first lock(). SRWLOCK
// is header/KERNEL32-only, always available under Wine, and constant-init
// (SRWLOCK_INIT) so it needs no global constructor. The offline host self-test
// (g++/Linux) keeps std::mutex.
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
namespace
{
	SRWLOCK gLock = SRWLOCK_INIT;
	struct ScopedLock
	{
		ScopedLock()  { AcquireSRWLockExclusive(&gLock); }
		~ScopedLock() { ReleaseSRWLockExclusive(&gLock); }
		ScopedLock(const ScopedLock&) = delete;
		ScopedLock& operator=(const ScopedLock&) = delete;
	};
}
#else
#  include <mutex>
namespace
{
	std::mutex gMx;
	struct ScopedLock { std::lock_guard<std::mutex> _g{gMx}; };
}
#endif

namespace
{
	// Base Stream::status field offset (shared by all Stream subclasses, both
	// binaries) and the Status enum values we set. The engine's own _read
	// writes status 3 (IllegalCall) on a capability failure; we mirror that
	// convention: 0 = Ok, 2 = EOS.
	constexpr std::size_t kStatusOff = 0x08;
	constexpr std::uint32_t kStatusOk  = 0;
	constexpr std::uint32_t kStatusEOS = 2;

	struct Entry
	{
		std::vector<std::uint8_t> plain;
		std::uint64_t pos = 0;
	};

	// Node-based: references/pointers to elements survive rehash, so a lookup
	// under the lock can hand back an Entry* used lock-free by the owning
	// thread (a FileStream is single-threaded for its lifetime).
	std::unordered_map<void*, Entry> gMap;

	std::atomic<bool> gFirstDecrypt{false};

	// Returns the entry for `self` or nullptr. Caller must not race a
	// concurrent FsUnregister(self) -- guaranteed by single-thread-per-stream.
	Entry* find(void* self)
	{
		ScopedLock lk;
		auto it = gMap.find(self);
		return it == gMap.end() ? nullptr : &it->second;
	}

	inline void setStatus(void* self, std::uint32_t s)
	{
		*reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(self) + kStatusOff) = s;
	}
}

namespace lfxe
{
	bool FsRegisterIfEncrypted(void* self,
	                           FsGetSizeFn origGetSize,
	                           FsSetPosFn  origSetPos,
	                           FsReadFn    origRead,
	                           KeyProvider& keys)
	{
		FsUnregister(self); // clear any stale entry from a prior use of this object

		const std::uint64_t size = origGetSize(self);
		if (size < kHeaderSize)
			return false;

		// Peek the magic before committing to a full slurp (cheap for vanilla).
		origSetPos(self, 0);
		std::uint8_t magic[4] = {0};
		if (!origRead(self, sizeof(magic), magic) || !HasMagic(magic, sizeof(magic)))
		{
			origSetPos(self, 0);
			return false;
		}

		std::vector<std::uint8_t> cipher;
		cipher.resize(static_cast<std::size_t>(size));
		origSetPos(self, 0);
		if (!origRead(self, size, cipher.data()))
		{
			origSetPos(self, 0);
			return false; // couldn't read it; let the engine deal with the raw file
		}

		std::vector<std::uint8_t> plain;
		if (TryDecryptAsset(cipher.data(), cipher.size(), keys, plain) != DecryptResult::Ok)
		{
			// Bad header / no key: leave the raw (still-encrypted) bytes so the
			// engine rejects them, rather than serving garbage.
			origSetPos(self, 0);
			return false;
		}

		{
			ScopedLock lk;
			Entry& e = gMap[self];
			e.plain = std::move(plain);
			e.pos   = 0;
		}
		origSetPos(self, 0);   // keep the engine's own position sane (we own reads now)
		setStatus(self, kStatusOk);
		gFirstDecrypt.store(true, std::memory_order_relaxed);
		return true;
	}

	void FsUnregister(void* self)
	{
		ScopedLock lk;
		gMap.erase(self); // frees the plaintext buffer via Entry's dtor
	}

	bool FsTryRead(void* self, uint64_t size, void* dst, bool* out)
	{
		Entry* e = find(self);
		if (!e) return false;

		const std::uint64_t avail = e->pos <= e->plain.size() ? e->plain.size() - e->pos : 0;
		const std::uint64_t take  = size <= avail ? size : avail;
		if (take && dst)
			std::memcpy(dst, e->plain.data() + e->pos, static_cast<std::size_t>(take));
		e->pos += take;

		const bool full = (take == size);
		setStatus(self, full ? kStatusOk : kStatusEOS);
		*out = full;
		return true;
	}

	bool FsTryGetSize(void* self, uint64_t* out)
	{
		Entry* e = find(self);
		if (!e) return false;
		*out = e->plain.size();
		return true;
	}

	bool FsTryGetPosition(void* self, uint64_t* out)
	{
		Entry* e = find(self);
		if (!e) return false;
		*out = e->pos;
		return true;
	}

	bool FsTrySetPosition(void* self, uint64_t pos, bool* out)
	{
		Entry* e = find(self);
		if (!e) return false;
		if (pos > e->plain.size())
		{
			*out = false; // seek past end: reject, matching the engine
			return true;
		}
		e->pos = pos;
		*out = true;
		return true;
	}

	bool FsConsumeFirstDecryptFlag()
	{
		return gFirstDecrypt.load(std::memory_order_relaxed);
	}
}
