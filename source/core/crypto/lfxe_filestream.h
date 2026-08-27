#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (shared client/server crypto).

	Universal LFXE decrypt at the engine FileStream layer (issue #116).

	RE (docs/lfxe_texture_re.md) established that the engine has exactly one
	FileStream class and that *every* real-file read funnels through
	FileStream::open -- called directly from 62 (client) / 23 (server) sites
	engine-wide, including the texture/DDS/PNG loaders. The earlier
	openFileStream *factory* hook (#115) only saw the minority of callers
	that go through the factory, which is why memory-resident textures and UI
	bitmaps slipped past it. Hooking the FileStream methods themselves covers
	them all and subsumes the DTS + DSO + factory hooks.

	FileStream is buffered (8 KB inline window), so rather than translate the
	24-byte LFXE header across buffer refills we decrypt the *whole* file into
	a heap buffer at open() and serve subsequent reads/seeks from it, keying
	per-object state on the FileStream pointer. The engine's own buffering is
	bypassed for LFXE-backed streams; vanilla streams are untouched
	(magic-gated -- a non-encrypted file never gets an entry).

	This module is ABI-agnostic: the per-DLL hook passes in the engine's
	original FileStream method pointers (Detours trampolines) so the same
	core works in both binaries. Thread-safe: textures load on worker
	threads, so the registry is mutex-guarded; per-entry data is only touched
	by the stream's owning thread, so reads/seeks run lock-free after lookup.
*  =================================================================================== */

#include <cstdint>

#include "key_provider.h"

namespace lfxe
{
	// Engine FileStream method ABI. Pointers below are the Detours trampolines
	// to the *original* methods. No calling-convention qualifier is needed:
	// on x64 __fastcall/__cdecl/__stdcall all collapse to the single Win64
	// convention (this in rcx), so the per-DLL hook's __fastcall trampolines
	// bind to these plain types without a cast -- and the shared core stays
	// compilable by a host toolchain for the off-engine self-test.
	using FsReadFn    = bool(*)(void* self, uint64_t size, void* dst);
	using FsSetPosFn  = bool(*)(void* self, uint64_t pos);
	using FsGetSizeFn = uint64_t(*)(void* self);

	// Called from the open() detour after the original open succeeded in read
	// mode. Reads the freshly opened stream via the supplied original methods;
	// if it is an LFXE container, decrypts the whole payload and registers
	// `self` as LFXE-backed (serving plaintext from memory thereafter).
	// Returns true iff registered. On a non-LFXE file, a bad header, or a
	// missing key it returns false AND clears any stale entry for `self`
	// (FileStream objects are reused), so the engine reads the raw bytes
	// (fail-safe: encrypted-but-undecryptable assets are rejected, not junk).
	bool FsRegisterIfEncrypted(void* self,
	                           FsGetSizeFn origGetSize,
	                           FsSetPosFn  origSetPos,
	                           FsReadFn    origRead,
	                           KeyProvider& keys);

	// Drop any state for `self` (call from the destructor detour and on a
	// non-read / failed re-open). Frees the plaintext buffer.
	void FsUnregister(void* self);

	// Accessor shims. Each returns true and fills *out when `self` is
	// LFXE-backed (the detour then returns *out without calling the original);
	// returns false otherwise (detour tail-calls the original method).
	bool FsTryRead(void* self, uint64_t size, void* dst, bool* out);
	bool FsTryGetSize(void* self, uint64_t* out);
	bool FsTryGetPosition(void* self, uint64_t* out);
	bool FsTrySetPosition(void* self, uint64_t pos, bool* out);

	// True once at least one stream has been decrypted this run (for the
	// per-DLL one-shot proof-of-life log). Latches; never resets.
	bool FsConsumeFirstDecryptFlag();
}
