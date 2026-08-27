/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.

	LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
	DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
	DEALINGS IN LIFX.
*  =================================================================================== */

#include "server/cm_server.h"
#include "lifx_effects.h"
#include "t3d_console.h"
#include "server/hooks/furnace/engine_internals.h"
#include "server/hooks/character/hook_set_control_object.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace
{
	// Empirical layout discovered from FUN_14009f730 (debug serializer at
	// docs/effects_and_abilities.md §"Resurrection sickness"):
	//
	//   character + 0x10E0          = effect-list container struct
	//   container + 0x148  (uint32) = count
	//   container + 0x150  (T**)    = std::list-style sentinel; *sentinel is
	//                                 the first real node, or sentinel itself
	//                                 if empty. Each node's first 8 bytes are
	//                                 the next-pointer; payload follows.
	//
	// These offsets are NOT yet verified for the effect-list specifically (the
	// same accessor pattern appears in unrelated unit-formation code at
	// character + 0x1B88). dumpEffects() is exactly the runtime probe we need
	// to confirm the offsets are correct: with a known character that we KNOW
	// has effects (e.g. immediately after applying a hunger debuff in-game),
	// a non-zero count + sensible payload bytes prove the layout.
	constexpr std::size_t kEffectContainerOffset = 0x10E0;
	constexpr std::size_t kCountFieldOffset      = 0x148;
	constexpr std::size_t kSentinelOffset        = 0x150;

	// Defensive iteration cap. If our offset guess is wrong we'd otherwise
	// follow garbage pointers indefinitely; stopping at 32 nodes keeps a bad
	// run from hanging the server console.
	constexpr unsigned kMaxNodesToWalk = 32;

	// How many payload bytes to print per node, starting from offset +0x10
	// (skip the next/prev pointers). 64 bytes is enough to see the effect ID
	// and any 4–8 byte duration/magnitude fields without flooding the console.
	constexpr unsigned kPayloadBytesToPrint = 64;

	std::uint32_t ParseCharId(S32 argc, const char* argv[], int slot)
	{
		if (slot >= argc || argv[slot] == nullptr || argv[slot][0] == '\0') return 0;
		return static_cast<std::uint32_t>(std::strtoul(argv[slot], nullptr, 10));
	}

	// Wide-scan probe. Walks a configurable byte range within the
	// CmCharacterInfo struct and prints every 8-byte qword that looks like
	// a potential container member, classifying each as:
	//   - "PTR-arena"  pointer in the same heap arena as the struct itself
	//   - "small-int"  uint32 < 256 in the low half (likely a count)
	//   - "ptr-self"   self-referencing (sentinel of an empty std::list)
	// Run this once with a known character to find the real effect container.
	//
	// Usage: Lifx::dumpCharScan(charID, startOffset, endOffset)
	//   offsets are hex like 0x1000 — wrap with quotes for the parser.
	void DumpCharScan(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		if (charID == 0) {
			Con::Warning("usage: Lifx::dumpCharScan(<charID>, <startOff>, <endOff>)  (offsets decimal or 0x-hex)");
			return;
		}
		const auto startOff = (argc > 2 && argv[2]) ? std::strtoul(argv[2], nullptr, 0) : 0x1000u;
		const auto endOff   = (argc > 3 && argv[3]) ? std::strtoul(argv[3], nullptr, 0) : 0x2200u;

		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::dumpCharScan: charID %u not found", charID);
			return;
		}

		auto* base = static_cast<unsigned char*>(ci);
		const auto ciAddr = reinterpret_cast<std::uintptr_t>(ci);

		// Estimate the heap arena: anything within ~16 MB of the struct's own
		// address is plausibly a sibling allocation from the same arena.
		constexpr std::uintptr_t kArenaSlack = 0x1000000ULL;

		Con::Echo("[lifx-effects] scan charID=%u  CmCharacterInfo=%p  range +0x%X..+0x%X",
		          charID, ci, (unsigned)startOff, (unsigned)endOff);

		for (std::size_t off = startOff; off + 8 <= endOff; off += 8) {
			const auto q = *reinterpret_cast<const std::uint64_t*>(base + off);
			if (q == 0) continue;

			const char* tag = nullptr;
			const auto self = reinterpret_cast<std::uintptr_t>(base + off);

			if (q == self) {
				tag = "ptr-self";   // empty std::list sentinel points to itself
			} else if (q >= ciAddr - kArenaSlack && q <= ciAddr + kArenaSlack) {
				tag = "PTR-arena";  // points into our heap arena
			} else if ((q >> 32) == 0 && (q & 0xFFFFFFFF) <= 256) {
				tag = "small-int";  // low half is a small int, high half zero
			}

			if (tag) {
				Con::Echo("[lifx-effects]   +0x%04zX  %016llX  %s",
				          off, (unsigned long long)q, tag);
			}
		}
		Con::Echo("[lifx-effects] scan done");
	}

	// Hunt for a specific effect ID inside any heap pointer that the
	// CmCharacterInfo struct holds. For every PTR-arena qword in the
	// scanned range, dereference and scan the first `kProbeBytes` bytes
	// for a uint32 equal to effectID. Report each hit with byte offset.
	//
	// Usage: Lifx::findEffect(charID, effectID [, hex startOff = 0x1000] [, hex endOff = 0x2200])
	void FindEffect(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		if (charID == 0 || argc < 3) {
			Con::Warning("usage: Lifx::findEffect(<charID>, <effectID> [, <startOff> [, <endOff>]])");
			return;
		}
		const auto effectID = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
		const auto startOff = (argc > 3 && argv[3]) ? std::strtoul(argv[3], nullptr, 0) : 0u;
		const auto endOff   = (argc > 4 && argv[4]) ? std::strtoul(argv[4], nullptr, 0) : 0x4000u;

		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::findEffect: charID %u not found", charID);
			return;
		}

		auto* base = static_cast<unsigned char*>(ci);
		const auto ciAddr = reinterpret_cast<std::uintptr_t>(ci);
		constexpr std::uintptr_t kArenaSlack = 0x1000000ULL;
		constexpr unsigned kProbeBytes = 128;

		Con::Echo("[lifx-effects] findEffect charID=%u effectID=%u  range +0x%X..+0x%X",
		          charID, effectID, (unsigned)startOff, (unsigned)endOff);

		unsigned hits = 0;

		// (A) Direct scan of the struct itself — catches inline storage.
		for (std::size_t off = 0; off + 4 <= endOff; off += 4) {
			const auto v = *reinterpret_cast<const std::uint32_t*>(base + off);
			if (v == effectID) {
				Con::Echo("[lifx-effects]   HIT-inline  charInfo+0x%04zX = %u", off, v);
				hits++;
			}
		}

		// (B) One level of pointer indirection, deeper probe (512 bytes).
		// (C) Two levels of pointer indirection.
		constexpr unsigned kProbeBytesL1 = 512;
		constexpr unsigned kProbeBytesL2 = 64;

		for (std::size_t off = startOff; off + 8 <= endOff; off += 8) {
			const auto q = *reinterpret_cast<const std::uint64_t*>(base + off);
			if (q == 0) continue;
			if (q < ciAddr - kArenaSlack || q > ciAddr + kArenaSlack) continue;

			auto* p = reinterpret_cast<const unsigned char*>(q);

			// (B) Scan first 512 bytes pointed at by q for effectID.
			for (unsigned probe = 0; probe + 4 <= kProbeBytesL1; probe += 4) {
				const auto v = *reinterpret_cast<const std::uint32_t*>(p + probe);
				if (v == effectID) {
					Con::Echo("[lifx-effects]   HIT-L1  container@+0x%04zX -> %p, effectID at *(+0x%03X) = %u",
					          off, (void*)q, probe, v);
					hits++;
				}
			}

			// (C) For each PTR-arena value INSIDE that block, follow once more.
			for (unsigned probe = 0; probe + 8 <= kProbeBytesL1; probe += 8) {
				const auto q2 = *reinterpret_cast<const std::uint64_t*>(p + probe);
				if (q2 == 0) continue;
				if (q2 < ciAddr - kArenaSlack || q2 > ciAddr + kArenaSlack) continue;
				auto* p2 = reinterpret_cast<const unsigned char*>(q2);
				for (unsigned probe2 = 0; probe2 + 4 <= kProbeBytesL2; probe2 += 4) {
					const auto v = *reinterpret_cast<const std::uint32_t*>(p2 + probe2);
					if (v == effectID) {
						Con::Echo("[lifx-effects]   HIT-L2  container@+0x%04zX -> %p -> +0x%03X -> %p +0x%02X = %u",
						          off, (void*)q, probe, (void*)q2, probe2, v);
						hits++;
					}
				}
			}
		}
		Con::Echo("[lifx-effects] findEffect done — %u hit(s)", hits);
	}

	// Hex-dump N bytes starting at character + offset. Used to inspect a
	// candidate effect entry once findEffect identifies the container.
	void DumpAt(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		if (charID == 0 || argc < 3) {
			Con::Warning("usage: Lifx::dumpAt(<charID>, <hex offset> [, <byteCount = 128>])");
			return;
		}
		const auto off = std::strtoul(argv[2], nullptr, 0);
		const auto count = (argc > 3 && argv[3]) ? std::strtoul(argv[3], nullptr, 0) : 128u;

		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) { Con::Warning("Lifx::dumpAt: charID %u not found", charID); return; }

		auto* p = static_cast<const unsigned char*>(ci) + off;
		Con::Echo("[lifx-effects] dumpAt charID=%u +0x%X = %p (%u bytes)",
		          charID, (unsigned)off, p, (unsigned)count);
		for (std::uint32_t i = 0; i < count; i += 16) {
			const auto* row = p + i;
			Con::Echo("[lifx-effects]   +0x%04X  %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
			          (unsigned)(off + i),
			          row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7],
			          row[8], row[9], row[10], row[11], row[12], row[13], row[14], row[15]);
		}
	}

	// Same as DumpAt but starting at an absolute pointer (e.g. one of the
	// container@+0x… values findEffect reports). Useful to inspect the
	// internal layout of an effect entry without re-walking the struct.
	void DumpPtr(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 2) {
			Con::Warning("usage: Lifx::dumpPtr(<hex pointer> [, <byteCount = 128>])");
			return;
		}
		const auto addr = std::strtoull(argv[1], nullptr, 0);
		const auto count = (argc > 2 && argv[2]) ? std::strtoul(argv[2], nullptr, 0) : 128u;
		const auto* p = reinterpret_cast<const unsigned char*>(addr);
		Con::Echo("[lifx-effects] dumpPtr %p (%u bytes)", p, (unsigned)count);
		for (std::uint32_t i = 0; i < count; i += 16) {
			const auto* row = p + i;
			Con::Echo("[lifx-effects]   +0x%04X  %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
			          (unsigned)i,
			          row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7],
			          row[8], row[9], row[10], row[11], row[12], row[13], row[14], row[15]);
		}
	}

	// Snapshot/diff state. One global slot is enough — we use this
	// interactively from the console, never concurrently. The snapshot stores
	// the raw bytes of a CmCharacterInfo subrange so a later diff can identify
	// any changed offsets (a ticking duration field gets caught regardless of
	// where it lives or how it's encoded).
	constexpr std::size_t kSnapMaxBytes = 0x10000;  // 64 KB
	struct Snap {
		bool       valid = false;
		bool       playerAnchored = false;
		std::uint32_t charID = 0;
		std::size_t startOff = 0;
		std::size_t endOff   = 0;
		unsigned char bytes[kSnapMaxBytes] = {};
	};
	Snap g_snap;

	void SnapshotChar(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		if (charID == 0) {
			Con::Warning("usage: Lifx::snapshotChar(<charID> [, <startOff = 0> [, <endOff = 0x10000>]])");
			return;
		}
		const auto startOff = (argc > 2 && argv[2]) ? std::strtoul(argv[2], nullptr, 0) : 0u;
		const auto endOff   = (argc > 3 && argv[3]) ? std::strtoul(argv[3], nullptr, 0) : kSnapMaxBytes;

		if (endOff <= startOff || endOff - startOff > kSnapMaxBytes) {
			Con::Warning("Lifx::snapshotChar: invalid range, max span = 0x%zX", kSnapMaxBytes);
			return;
		}

		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::snapshotChar: charID %u not found", charID);
			return;
		}

		const auto span = endOff - startOff;
		std::memcpy(g_snap.bytes, static_cast<unsigned char*>(ci) + startOff, span);
		g_snap.valid    = true;
		g_snap.playerAnchored = false;
		g_snap.charID   = charID;
		g_snap.startOff = startOff;
		g_snap.endOff   = endOff;
		Con::Echo("[lifx-effects] snapshotChar  charID=%u  range +0x%X..+0x%X  (%zu bytes captured)",
		          charID, (unsigned)startOff, (unsigned)endOff, span);
	}

	void DiffChar(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (!g_snap.valid) {
			Con::Warning("Lifx::diffChar: no snapshot yet — run Lifx::snapshotChar(<charID>) first");
			return;
		}
		if (g_snap.playerAnchored) {
			Con::Warning("Lifx::diffChar: last snapshot is Player-anchored — use Lifx::diffPlayer() instead");
			return;
		}
		// Read live bytes for the same charID/range and diff against the saved
		// snapshot at 4-byte granularity.
		void* ci = ::Engine::Character_GetByID(g_snap.charID);
		if (!ci) {
			Con::Warning("Lifx::diffChar: charID %u not found (relogged?)", g_snap.charID);
			return;
		}

		auto* base = static_cast<unsigned char*>(ci);
		Con::Echo("[lifx-effects] diffChar  charID=%u  range +0x%X..+0x%X",
		          g_snap.charID, (unsigned)g_snap.startOff, (unsigned)g_snap.endOff);

		unsigned changed = 0;
		const auto span = g_snap.endOff - g_snap.startOff;
		for (std::size_t i = 0; i + 4 <= span; i += 4) {
			const auto before = *reinterpret_cast<const std::uint32_t*>(g_snap.bytes + i);
			const auto after  = *reinterpret_cast<const std::uint32_t*>(base + g_snap.startOff + i);
			if (before == after) continue;
			const auto off = g_snap.startOff + i;
			const std::int64_t delta = static_cast<std::int64_t>(after) - static_cast<std::int64_t>(before);
			Con::Echo("[lifx-effects]   CHANGED  +0x%04zX  %08X -> %08X  (delta %lld)",
			          off, before, after, (long long)delta);
			changed++;
			if (changed > 256) {
				Con::Warning("[lifx-effects]   capped at 256 changes — too noisy, narrow the range");
				return;
			}
		}
		Con::Echo("[lifx-effects] diffChar done — %u 4-byte word(s) changed", changed);
	}

	// Resolve a Player* given a character ID.
	//
	// First attempt was `Player = Character_GetByID(charID) - 0xAA8` based on
	// engine_internals.h's note. That was rejected in practice (the stamp
	// read at -0xAA8 + 0x1B44 didn't match the requested charID).
	//
	// Fallback: walk the CmCharacterInfo struct's first ~64 KB looking for
	// any user-space pointer (high half == 0x7F* on Linux/wine) whose target
	// contains the charID at byte offset +0x1B44 (the documented Player
	// charID field). Return the first such target.
	//
	// `verbose=true` echoes every candidate hit so the user can verify or
	// pick a specific Player among multiple matches.
	void* FindPlayerByScan(std::uint32_t charID, bool verbose)
	{
		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) return nullptr;
		auto* base = static_cast<unsigned char*>(ci);
		const auto ciAddr = reinterpret_cast<std::uintptr_t>(ci);

		constexpr std::size_t kScanRange    = 0x10000;
		constexpr std::uintptr_t kArenaMask = 0xFFFF000000000000ULL;
		// Plausible user-space heap pointers in wine have the top 16 bits
		// in 0x0000 (canonical low-half pointers). We also require they
		// are not a self-reference into the CmCharacterInfo itself.
		const auto selfArenaMin = ciAddr & ~0xFFFFFFFFULL;
		const auto selfArenaMax = selfArenaMin + 0x100000000ULL;

		for (std::size_t off = 0; off + 8 <= kScanRange; off += 8) {
			const auto q = *reinterpret_cast<const std::uint64_t*>(base + off);
			if (q == 0) continue;
			if ((q & kArenaMask) != 0) continue;            // not a canonical low-half ptr
			if (q < selfArenaMin || q > selfArenaMax) continue;  // far from our arena, skip
			// Avoid pointers into our own struct (those wouldn't be Player).
			if (q >= ciAddr && q < ciAddr + kScanRange) continue;

			// Test the candidate: read uint32 at q + 0x1B44 and compare to charID.
			const auto* p = reinterpret_cast<const unsigned char*>(q);
			const auto stamped = *reinterpret_cast<const std::uint32_t*>(p + 0x1B44);
			if (stamped != charID) continue;

			if (verbose) {
				Con::Echo("[lifx-effects] findPlayer candidate: charInfo+0x%04zX -> %p  (stamp at +0x1B44 = %u)",
				          off, (void*)q, stamped);
			}
			return (void*)q;
		}
		return nullptr;
	}

	// Backward-compatible wrapper: previously did -0xAA8. Now scans.
	void* PlayerFromCharID(std::uint32_t charID)
	{
		void* p = FindPlayerByScan(charID, /*verbose=*/false);
		if (!p) {
			Con::Warning("[lifx-effects] PlayerFromCharID(%u): no Player pointer found in CmCharacterInfo scan — character may not be in-world, or charID-stamp offset differs from +0x1B44",
			             charID);
		}
		return p;
	}

	// ---------------------------------------------------------------------
	// Player-method variants. Registered under the "Player" Torque namespace
	// so they're callable as `%player.lifxSnapshotEffects()`. The engine
	// passes the Player* as `obj` automatically when invoking a method on a
	// Player SimObject — no charID lookup needed.
	//
	// Idiomatic in-game usage (combined with the user's LiFxUtility::getPlayer
	// helper that walks ClientGroup):
	//
	//   %c = LiFxUtility::getPlayer(1);
	//   %p = %c.getControlObject();
	//   %p.lifxSnapshotEffects();
	//   // ...do something (die, cast ability, wait)...
	//   %p.lifxDiffEffects();
	// ---------------------------------------------------------------------

	// NOTE on argv layout for `Player`-namespace methods: when invoked as
	// `%p.method(arg1, arg2)`, Torque sets argv[0]=method-name,
	// argv[1]=stringified object ID, argv[2]=arg1, argv[3]=arg2. We must
	// read user args from argv[2] onward, NOT argv[1] (otherwise the SimID
	// gets misinterpreted as the first argument — verified empirically when
	// `%p.lifxFindEffect(66)` was searching for effectID=1849 instead).
	void SnapshotPlayerThis(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("Player.lifxSnapshotEffects: no Player object"); return; }
		const auto startOff = (argc > 2 && argv[2]) ? std::strtoul(argv[2], nullptr, 0) : 0u;
		const auto endOff   = (argc > 3 && argv[3]) ? std::strtoul(argv[3], nullptr, 0) : kSnapMaxBytes;
		if (endOff <= startOff || endOff - startOff > kSnapMaxBytes) {
			Con::Warning("Player.lifxSnapshotEffects: invalid range, max span = 0x%zX", kSnapMaxBytes);
			return;
		}
		const auto span = endOff - startOff;
		std::memcpy(g_snap.bytes, static_cast<unsigned char*>(obj) + startOff, span);
		g_snap.valid = true;
		g_snap.playerAnchored = true;
		g_snap.charID = 0;  // unused in this mode; obj is the anchor
		g_snap.startOff = startOff;
		g_snap.endOff   = endOff;
		Con::Echo("[lifx-effects] Player.lifxSnapshotEffects  Player=%p  range +0x%X..+0x%X  (%zu bytes)",
		          obj, (unsigned)startOff, (unsigned)endOff, span);
	}

	void DiffPlayerThis(LPVOID obj, S32 /*argc*/, const char* /*argv*/[])
	{
		if (!obj) { Con::Warning("Player.lifxDiffEffects: no Player object"); return; }
		if (!g_snap.valid || !g_snap.playerAnchored) {
			Con::Warning("Player.lifxDiffEffects: no Player-anchored snapshot — run %%p.lifxSnapshotEffects() first");
			return;
		}

		auto* base = static_cast<unsigned char*>(obj);
		Con::Echo("[lifx-effects] Player.lifxDiffEffects  Player=%p  range +0x%X..+0x%X",
		          obj, (unsigned)g_snap.startOff, (unsigned)g_snap.endOff);

		unsigned changed = 0;
		const auto span = g_snap.endOff - g_snap.startOff;
		for (std::size_t i = 0; i + 4 <= span; i += 4) {
			const auto before = *reinterpret_cast<const std::uint32_t*>(g_snap.bytes + i);
			const auto after  = *reinterpret_cast<const std::uint32_t*>(base + g_snap.startOff + i);
			if (before == after) continue;
			const auto off = g_snap.startOff + i;
			const std::int64_t delta = static_cast<std::int64_t>(after) - static_cast<std::int64_t>(before);
			Con::Echo("[lifx-effects]   CHANGED  +0x%04zX  %08X -> %08X  (delta %lld)",
			          off, before, after, (long long)delta);
			changed++;
			if (changed > 256) {
				Con::Warning("[lifx-effects]   capped at 256 — narrow the range");
				return;
			}
		}
		Con::Echo("[lifx-effects] Player.lifxDiffEffects done — %u 4-byte word(s) changed", changed);
	}

	// Inline + L1 + L2 effectID search anchored at *this Player.
	void FindEffectOnPlayer(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("Player.lifxFindEffect: no Player object"); return; }
		if (argc < 3) { Con::Warning("usage: %%p.lifxFindEffect(<effectID> [, <startOff> [, <endOff>]])"); return; }
		const auto effectID = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
		const auto startOff = (argc > 3 && argv[3]) ? std::strtoul(argv[3], nullptr, 0) : 0u;
		const auto endOff   = (argc > 4 && argv[4]) ? std::strtoul(argv[4], nullptr, 0) : 0x10000u;

		auto* base = static_cast<unsigned char*>(obj);
		const auto playerAddr = reinterpret_cast<std::uintptr_t>(obj);
		constexpr std::uintptr_t kArenaSlack = 0x1000000ULL;

		Con::Echo("[lifx-effects] Player.lifxFindEffect  Player=%p  effectID=%u  range +0x%X..+0x%X",
		          obj, effectID, (unsigned)startOff, (unsigned)endOff);

		unsigned hits = 0;
		// Inline scan.
		for (std::size_t off = startOff; off + 4 <= endOff; off += 4) {
			const auto v = *reinterpret_cast<const std::uint32_t*>(base + off);
			if (v == effectID) {
				Con::Echo("[lifx-effects]   HIT-inline  Player+0x%04zX = %u", off, v);
				hits++;
			}
		}
		// L1: follow same-arena pointers and probe 512 bytes.
		for (std::size_t off = startOff; off + 8 <= endOff; off += 8) {
			const auto q = *reinterpret_cast<const std::uint64_t*>(base + off);
			if (q == 0) continue;
			if (q < playerAddr - kArenaSlack || q > playerAddr + kArenaSlack) continue;
			auto* p = reinterpret_cast<const unsigned char*>(q);
			for (unsigned probe = 0; probe + 4 <= 512; probe += 4) {
				const auto v = *reinterpret_cast<const std::uint32_t*>(p + probe);
				if (v == effectID) {
					Con::Echo("[lifx-effects]   HIT-L1  Player+0x%04zX -> %p +0x%03X = %u",
					          off, (void*)q, probe, v);
					hits++;
				}
			}
		}
		Con::Echo("[lifx-effects] Player.lifxFindEffect done — %u hit(s)", hits);
	}

	// Hex dump from Player + offset. Offset must fit in 32-bit (Torque truncates).
	void DumpAtPlayer(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("Player.lifxDumpAt: no Player object"); return; }
		if (argc < 3) { Con::Warning("usage: %%p.lifxDumpAt(<hex offset>, <byteCount>)"); return; }
		const auto off = std::strtoul(argv[2], nullptr, 0);
		const auto count = (argc > 3 && argv[3]) ? std::strtoul(argv[3], nullptr, 0) : 64u;
		auto* p = static_cast<const unsigned char*>(obj) + off;
		Con::Echo("[lifx-effects] Player.lifxDumpAt  Player=%p  +0x%X (%u bytes)",
		          obj, (unsigned)off, (unsigned)count);
		for (std::uint32_t i = 0; i < count; i += 16) {
			const auto* row = p + i;
			Con::Echo("[lifx-effects]   +0x%04X  %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
			          (unsigned)(off + i),
			          row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7],
			          row[8], row[9], row[10], row[11], row[12], row[13], row[14], row[15]);
		}
	}

	// Write a uint32 into this Player at byte offset `argv[2]`. Used to test
	// whether we've correctly identified the expires_at field by pushing it
	// far into the future and observing the in-game effect-timer change.
	//
	// Usage: %p.lifxPokeU32(<hex offset>, <hex value>)
	void PokeU32Player(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("Player.lifxPokeU32: no Player object"); return; }
		if (argc < 4) { Con::Warning("usage: %%p.lifxPokeU32(<hex offset>, <hex value>)"); return; }
		const auto off = std::strtoul(argv[2], nullptr, 0);
		const auto val = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 0));
		auto* slot = reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(obj) + off);
		const auto before = *slot;
		*slot = val;
		Con::Echo("[lifx-effects] Player.lifxPokeU32  Player=%p  +0x%X  %08X -> %08X",
		          obj, (unsigned)off, before, val);
	}

	// Verified layout of Table A (Player active-effect table). Each row is
	// 24 bytes, indexed by effect ID. Row 0 starts at Player+0x1238.
	//
	//   row+0  (uint32)  expires_at_ms   absolute global-clock when expires
	//   row+4  (uint32)  applied_at_ms   absolute global-clock when applied
	//   row+8  (uint32)  effect ID       self-reference (matches the index)
	//   row+12 (uint32)  metadata        flags / UI state
	//   row+16 (uint32)  magnitude × 1e6 active intensity
	//   row+20 (uint32)  unused
	//
	// Confirmed by:
	//   - dumpAt of Player+0x16A0..+0x16BF with Resurrected active:
	//       expires_at=793897  applied_at=193897   diff=600000 ms = 10 min ✓
	//   - PokeU32 of row+0 with a past value caused Resurrected's HP_MAX
	//     penalty + regen reduction to expire server-side (client UI did
	//     not refresh — countdown is computed locally on the client).
	constexpr std::size_t kPlayerEffectTableBase = 0x1238;
	constexpr std::size_t kPlayerEffectRowSize   = 24;
	constexpr std::size_t kExpiresAtOffset = 0;
	constexpr std::size_t kAppliedAtOffset = 4;
	constexpr std::size_t kEffectIDOffset  = 8;
	constexpr std::size_t kMagnitudeOffset = 16;

	// ObjEffectsEvent broadcast path (see Ghidra trace in #32):
	//   - Player vtable slot 187 = effect-delta broadcaster (FUN_1400ebff0)
	//     which constructs an ObjEffectsEvent (option=3 / per-row delta) and
	//     posts it to every NetConnection ghosting the Player.
	//   - Client's process() handler walks the delta list and calls
	//     cObjEffects::Assign_effect(player+0xAA8, effectID, &row16) per entry.
	//     That updates the client's local effect manager AND triggers the
	//     HUD refresh (no separate "remove effect" message needed).
	//   - Delta entry is 32 bytes:
	//       [0..4]   effect ID  (key for Assign_effect)
	//       [4..8]   pad
	//       [8..12]  expires_at_ms
	//       [12..16] applied_at_ms
	//       [16..24] magnitude_data (u64 — mirrors row+16..23)
	//       [24..32] pad
	//   - DeltaList is a std::vector-shape: {begin, end, end_of_storage}.
	constexpr std::size_t kPlayerBroadcastVtableSlot = 187;

	struct EffectDelta {
		std::uint32_t effect_id;
		std::uint32_t pad1;
		std::uint32_t expires_at_ms;
		std::uint32_t applied_at_ms;
		std::uint64_t magnitude_data;
		std::uint64_t pad2;
	};
	static_assert(sizeof(EffectDelta) == 32, "ObjEffectsEvent delta entry is 32 bytes");

	struct DeltaVec {
		EffectDelta* begin;
		EffectDelta* end;
		EffectDelta* end_of_storage;
	};

	// Send a single-effect change to all clients ghosting this Player.
	// Returns true if the call dispatched (vtable slot was non-null).
	bool BroadcastEffectDelta(LPVOID player, std::uint32_t effectID)
	{
		if (!player) return false;
		auto* row = static_cast<unsigned char*>(player)
		          + kPlayerEffectTableBase + effectID * kPlayerEffectRowSize;

		EffectDelta delta{};
		delta.effect_id     = effectID;
		delta.expires_at_ms = *reinterpret_cast<std::uint32_t*>(row + kExpiresAtOffset);
		delta.applied_at_ms = *reinterpret_cast<std::uint32_t*>(row + kAppliedAtOffset);
		delta.magnitude_data = *reinterpret_cast<std::uint64_t*>(row + 16);

		DeltaVec vec{ &delta, &delta + 1, &delta + 1 };

		auto* vt = *reinterpret_cast<void***>(player);
		if (!vt) { Con::Warning("[lifx-effects] broadcast: null Player vtable"); return false; }
		using BroadcastFn = bool(*)(void*, void*, void*);
		auto fn = reinterpret_cast<BroadcastFn>(vt[kPlayerBroadcastVtableSlot]);
		if (!fn) { Con::Warning("[lifx-effects] broadcast: null vtable slot %u",
		                         (unsigned)kPlayerBroadcastVtableSlot); return false; }
		fn(player, nullptr, &vec);
		return true;
	}

	// Sanity-check and return a pointer to row N in Table A, or nullptr.
	std::uint32_t* PlayerEffectRow(LPVOID obj, std::uint32_t effectID, std::size_t fieldOffset)
	{
		const auto rowBase = kPlayerEffectTableBase + effectID * kPlayerEffectRowSize;
		auto* row = static_cast<unsigned char*>(obj) + rowBase;
		const auto stamp = *reinterpret_cast<const std::uint32_t*>(row + kEffectIDOffset);
		if (stamp != effectID) {
			Con::Warning("[lifx-effects] PlayerEffectRow: row %u at +0x%X self-ID stamp = %u (expected %u); refusing",
			             (unsigned)effectID, (unsigned)rowBase, (unsigned)stamp, (unsigned)effectID);
			return nullptr;
		}
		return reinterpret_cast<std::uint32_t*>(row + fieldOffset);
	}

	// Set the expires_at field of one effect row in Table A.
	// Usage: %p.lifxSetEffectExpiry(<effectID>, <expires_at_ms>)
	void SetEffectExpiry(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("Player.lifxSetEffectExpiry: no Player object"); return; }
		if (argc < 4) { Con::Warning("usage: %%p.lifxSetEffectExpiry(<effectID>, <expires_at_ms>)"); return; }
		const auto effectID = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
		const auto expiry   = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 0));
		auto* expSlot = PlayerEffectRow(obj, effectID, kExpiresAtOffset);
		if (!expSlot) return;
		const auto before = *expSlot;
		*expSlot = expiry;
		const bool broadcast = BroadcastEffectDelta(obj, effectID);
		Con::Echo("[lifx-effects] Player.lifxSetEffectExpiry  effectID=%u  expires_at: %u -> %u  broadcast=%d",
		          (unsigned)effectID, (unsigned)before, (unsigned)expiry, (int)broadcast);
	}

	// Extend an active effect's expires_at by N ms.
	// Usage: %p.lifxExtendEffect(<effectID>, <extra_ms>)
	void ExtendEffect(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("Player.lifxExtendEffect: no Player object"); return; }
		if (argc < 4) { Con::Warning("usage: %%p.lifxExtendEffect(<effectID>, <extra_ms>)"); return; }
		const auto effectID = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
		const auto extraMs  = std::strtoul(argv[3], nullptr, 0);
		auto* expSlot = PlayerEffectRow(obj, effectID, kExpiresAtOffset);
		if (!expSlot) return;
		const auto before = *expSlot;
		*expSlot = before + static_cast<std::uint32_t>(extraMs);
		const bool broadcast = BroadcastEffectDelta(obj, effectID);
		Con::Echo("[lifx-effects] Player.lifxExtendEffect  effectID=%u  expires_at: %u -> %u (+%u ms)  broadcast=%d",
		          (unsigned)effectID, (unsigned)before, (unsigned)*expSlot, (unsigned)extraMs, (int)broadcast);
	}

	// Broadcast the current row state to every client without modifying it.
	// Lets us verify the broadcast vtable slot in isolation from any write.
	// Usage: %p.lifxBroadcastEffect(<effectID>)
	void BroadcastEffectCmd(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("Player.lifxBroadcastEffect: no Player object"); return; }
		if (argc < 3) { Con::Warning("usage: %%p.lifxBroadcastEffect(<effectID>)"); return; }
		const auto effectID = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
		// validate the row first
		if (!PlayerEffectRow(obj, effectID, kExpiresAtOffset)) return;
		const bool ok = BroadcastEffectDelta(obj, effectID);
		Con::Echo("[lifx-effects] Player.lifxBroadcastEffect  effectID=%u  broadcast=%d",
		          (unsigned)effectID, (int)ok);
	}

	// Clear an effect immediately (set expires_at well before applied_at and
	// zero the magnitude so the engine sees no residual penalty on
	// re-evaluation). Single-call: no flicker, no need to re-issue.
	// Usage: %p.lifxClearEffect(<effectID>)
	void ClearEffect(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("Player.lifxClearEffect: no Player object"); return; }
		if (argc < 3) { Con::Warning("usage: %%p.lifxClearEffect(<effectID>)"); return; }
		const auto effectID = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
		auto* expSlot = PlayerEffectRow(obj, effectID, kExpiresAtOffset);
		auto* magSlot = PlayerEffectRow(obj, effectID, kMagnitudeOffset);
		if (!expSlot || !magSlot) return;
		const auto beforeExp = *expSlot;
		const auto beforeMag = *magSlot;
		*expSlot = 0;        // unambiguously in the past
		*magSlot = 0;        // no penalty to apply on re-evaluation
		const bool broadcast = BroadcastEffectDelta(obj, effectID);
		Con::Echo("[lifx-effects] Player.lifxClearEffect  effectID=%u  expires_at: %u -> 0  magnitude: %u -> 0  broadcast=%d",
		          (unsigned)effectID, (unsigned)beforeExp, (unsigned)beforeMag, (int)broadcast);
	}

	// Chunk-13 RE tool (issue #97). Scans the Player struct for plausible
	// 3-float position vectors. Default range 0x800 (2 KB) is sized to
	// cover the SceneObject prefix where Torque3D keeps mObjToWorld.
	//
	// Two modes:
	//   Lifx::dumpPlayerPos(charID)                    — plausibility-filtered
	//   Lifx::dumpPlayerPos(charID, knownX, knownY, knownZ)
	//                                                  — search for exact match
	void DumpPlayerPos(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		if (charID == 0) { Con::Warning("usage: Lifx::dumpPlayerPos(<charID> [, knownX, knownY, knownZ])"); return; }

		const bool exact = (argc >= 5 && argv[2] && argv[3] && argv[4]);
		const float kx = exact ? std::strtof(argv[2], nullptr) : 0.f;
		const float ky = exact ? std::strtof(argv[3], nullptr) : 0.f;
		const float kz = exact ? std::strtof(argv[4], nullptr) : 0.f;

		// Prefer the hook-captured pointer — it's exactly what the engine
		// handed to setControlObject (issue #99). PlayerFromCharID can
		// return a stale heap allocation whose +0x1B44 just happens to
		// be 1, sending the scan into unmapped memory and silently
		// hanging the TS callback.
		void* player = Hooks::SetControlObject::LastControlledPlayer();
		if (!player) {
			player = PlayerFromCharID(charID);
			if (!player) {
				Con::Warning("[lifx-pos] no Player available — setControlObject hook hasn't fired yet (log a character in) and charID scan also failed");
				return;
			}
			Con::Echo("[lifx-pos] using PlayerFromCharID fallback Player=%p (hook capture empty)", player);
		} else {
			Con::Echo("[lifx-pos] using hook-captured Player=%p", player);
		}

		auto* base = static_cast<const unsigned char*>(player);
		// Plausibility mode stays narrow (2 KB) — wider ranges spam.
		// Exact-match mode goes wide (64 KB) — it never logs non-matches,
		// so it's safe to read past the SceneObject prefix into the rest
		// of the Player allocation. snapshotPlayer uses the same 0x10000
		// bound without issue, so 64 KB is a known-safe ceiling.
		const std::size_t kScanBytes = exact ? 0x10000 : 0x800;
		std::size_t hits = 0;

		if (exact)
			Con::Echo("[lifx-pos] scanning Player=%p for exact match (%.4f, %.4f, %.4f) tol=1.0 (range +0x000..+0x%zX)",
			          player, kx, ky, kz, kScanBytes);
		else
			Con::Echo("[lifx-pos] scanning Player=%p for plausible 3-float vectors (range +0x000..+0x%zX)",
			          player, kScanBytes);

		for (std::size_t off = 0; off + 12 <= kScanBytes; off += 4)
		{
			const float x = *reinterpret_cast<const float*>(base + off + 0);
			const float y = *reinterpret_cast<const float*>(base + off + 4);
			const float z = *reinterpret_cast<const float*>(base + off + 8);

			if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) continue;

			if (exact)
			{
				if (std::fabs(x - kx) > 1.0f) continue;
				if (std::fabs(y - ky) > 1.0f) continue;
				if (std::fabs(z - kz) > 1.0f) continue;
				Con::Echo("[lifx-pos] +0x%04zX: (%.4f, %.4f, %.4f)  <-- MATCH", off, x, y, z);
				++hits;
				continue;
			}

			constexpr float kMaxAbs = 100000.0f;
			if (std::fabs(x) > kMaxAbs || std::fabs(y) > kMaxAbs || std::fabs(z) > kMaxAbs) continue;
			if (std::fabs(x) < 0.01f && std::fabs(y) < 0.01f && std::fabs(z) < 0.01f) continue;
			if (std::fabs(x) < 1.0f  && std::fabs(y) < 1.0f  && std::fabs(z) < 1.0f)  continue;

			Con::Echo("[lifx-pos] +0x%04zX: (%.3f, %.3f, %.3f)", off, x, y, z);
			if (++hits >= 64) {
				Con::Echo("[lifx-pos] (truncated at 64 candidates)");
				break;
			}
		}
		Con::Echo("[lifx-pos] done, %zu candidate(s)%s",
		          hits, exact ? "" : ". Move + re-run; the candidate that changed is the live position.");
	}

	void FindPlayer(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		if (charID == 0) { Con::Warning("usage: Lifx::findPlayer(<charID>)"); return; }
		Con::Echo("[lifx-effects] findPlayer charID=%u (scanning CmCharacterInfo for any pointer whose target has charID at +0x1B44)",
		          charID);
		void* p = FindPlayerByScan(charID, /*verbose=*/true);
		if (p) Con::Echo("[lifx-effects] findPlayer => Player ~= %p", p);
		else   Con::Echo("[lifx-effects] findPlayer => no match");
	}

	// Same shape as snapshotChar/diffChar but anchored at the Player object.
	void SnapshotPlayer(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		if (charID == 0) {
			Con::Warning("usage: Lifx::snapshotPlayer(<charID> [, <startOff = 0> [, <endOff = 0x10000>]])");
			return;
		}
		const auto startOff = (argc > 2 && argv[2]) ? std::strtoul(argv[2], nullptr, 0) : 0u;
		const auto endOff   = (argc > 3 && argv[3]) ? std::strtoul(argv[3], nullptr, 0) : kSnapMaxBytes;
		if (endOff <= startOff || endOff - startOff > kSnapMaxBytes) {
			Con::Warning("Lifx::snapshotPlayer: invalid range, max span = 0x%zX", kSnapMaxBytes);
			return;
		}

		void* player = PlayerFromCharID(charID);
		if (!player) return;

		const auto span = endOff - startOff;
		std::memcpy(g_snap.bytes, static_cast<unsigned char*>(player) + startOff, span);
		g_snap.valid    = true;
		g_snap.charID   = charID;
		g_snap.startOff = startOff;
		g_snap.endOff   = endOff;
		// Re-purpose the snap slot. diffChar already knows the range; we
		// mark this snapshot as Player-anchored so diffPlayer can verify.
		g_snap.playerAnchored = true;
		Con::Echo("[lifx-effects] snapshotPlayer  charID=%u  Player=%p  range +0x%X..+0x%X  (%zu bytes)",
		          charID, player, (unsigned)startOff, (unsigned)endOff, span);
	}

	void DiffPlayer(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (!g_snap.valid || !g_snap.playerAnchored) {
			Con::Warning("Lifx::diffPlayer: no Player-anchored snapshot — run Lifx::snapshotPlayer(<charID>) first");
			return;
		}
		void* player = PlayerFromCharID(g_snap.charID);
		if (!player) return;

		auto* base = static_cast<unsigned char*>(player);
		Con::Echo("[lifx-effects] diffPlayer  charID=%u  Player=%p  range +0x%X..+0x%X",
		          g_snap.charID, player, (unsigned)g_snap.startOff, (unsigned)g_snap.endOff);

		unsigned changed = 0;
		const auto span = g_snap.endOff - g_snap.startOff;
		for (std::size_t i = 0; i + 4 <= span; i += 4) {
			const auto before = *reinterpret_cast<const std::uint32_t*>(g_snap.bytes + i);
			const auto after  = *reinterpret_cast<const std::uint32_t*>(base + g_snap.startOff + i);
			if (before == after) continue;
			const auto off = g_snap.startOff + i;
			const std::int64_t delta = static_cast<std::int64_t>(after) - static_cast<std::int64_t>(before);
			Con::Echo("[lifx-effects]   CHANGED  +0x%04zX  %08X -> %08X  (delta %lld)",
			          off, before, after, (long long)delta);
			changed++;
			if (changed > 256) {
				Con::Warning("[lifx-effects]   capped at 256 — narrow the range");
				return;
			}
		}
		Con::Echo("[lifx-effects] diffPlayer done — %u 4-byte word(s) changed", changed);
	}

	void DumpEffects(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		if (charID == 0) {
			Con::Warning("usage: Lifx::dumpEffects(<charID>)");
			return;
		}

		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::dumpEffects: charID %u not found (character not online?)", charID);
			return;
		}

		auto* base = static_cast<unsigned char*>(ci);
		auto* container = base + kEffectContainerOffset;
		const auto count = *reinterpret_cast<const std::uint32_t*>(container + kCountFieldOffset);
		auto** sentinel = reinterpret_cast<void**>(container + kSentinelOffset);

		Con::Echo("[lifx-effects] charID=%u  CmCharacterInfo=%p", charID, ci);
		Con::Echo("[lifx-effects]   container @+0x%zX = %p", kEffectContainerOffset, container);
		Con::Echo("[lifx-effects]   count    @+0x%zX = %u", kEffectContainerOffset + kCountFieldOffset, count);
		Con::Echo("[lifx-effects]   sentinel @+0x%zX = %p   *sentinel = %p",
		          kEffectContainerOffset + kSentinelOffset, sentinel, *sentinel);

		if (count == 0 || *sentinel == sentinel) {
			Con::Echo("[lifx-effects]   (empty)");
			return;
		}

		void* node = *sentinel;
		for (unsigned i = 0; i < kMaxNodesToWalk; ++i) {
			if (node == nullptr || node == sentinel) {
				Con::Echo("[lifx-effects]   end after %u node(s)", i);
				return;
			}

			auto* p = static_cast<unsigned char*>(node);

			// Emit the first 16 payload-significant uint32s as a fast scan
			// table — easier to spot an effect ID, a duration ms field, etc.
			Con::Echo("[lifx-effects]   node[%u] @ %p", i, node);
			for (unsigned off = 0x00; off + 4 <= 0x10 + kPayloadBytesToPrint; off += 4) {
				const auto u = *reinterpret_cast<const std::uint32_t*>(p + off);
				Con::Echo("[lifx-effects]     +0x%02X = 0x%08X (%u)", off, u, u);
			}

			// Follow the next pointer.
			node = *reinterpret_cast<void**>(p);
		}
		Con::Warning("[lifx-effects]   walk capped at %u nodes — your offset guess may be wrong (loop or non-list)", kMaxNodesToWalk);
	}
}

void Lifx::Api::Effects::Register()
{
	Con::AddCommand("Lifx", "dumpEffects", &DumpEffects,
	                "(int charID) - dump the raw effect-list bytes for a character (investigation tool for issue #30)",
	                2, 2);
	Con::AddCommand("Lifx", "dumpCharScan", &DumpCharScan,
	                "(int charID [, hex startOff = 0x1000] [, hex endOff = 0x2200]) - wide scan of the CmCharacterInfo struct looking for plausible containers",
	                2, 4);
	Con::AddCommand("Lifx", "findEffect", &FindEffect,
	                "(int charID, int effectID [, hex startOff [, hex endOff]]) - find a known effect ID inside any heap pointer held by the character struct",
	                3, 5);
	Con::AddCommand("Lifx", "dumpAt", &DumpAt,
	                "(int charID, hex offset [, byteCount]) - hex dump bytes at character + offset",
	                3, 4);
	Con::AddCommand("Lifx", "dumpPtr", &DumpPtr,
	                "(hex pointer [, byteCount]) - hex dump bytes at an absolute address (e.g. a container hit from findEffect)",
	                2, 3);
	Con::AddCommand("Lifx", "snapshotChar", &SnapshotChar,
	                "(int charID [, hex startOff = 0] [, hex endOff = 0x10000]) - save the bytes of the character struct subrange",
	                2, 4);
	Con::AddCommand("Lifx", "diffChar", &DiffChar,
	                "() - diff the saved snapshot against current bytes; reports every 4-byte word that changed",
	                1, 1);
	Con::AddCommand("Lifx", "dumpPlayerPos", &DumpPlayerPos,
	                "(int charID [, float knownX, knownY, knownZ]) - scan the Player struct (first 2 KB) for 3-float position vectors. With known x/y/z, prints exact matches; without, prints plausibility-filtered candidates (issue #97).",
	                2, 5);
	Con::AddCommand("Lifx", "snapshotPlayer", &SnapshotPlayer,
	                "(int charID [, hex startOff = 0] [, hex endOff = 0x10000]) - snapshot the Player ShapeBase (resolved by scanning CmCharacterInfo for a pointer whose target has charID at +0x1B44)",
	                2, 4);
	Con::AddCommand("Lifx", "diffPlayer", &DiffPlayer,
	                "() - diff a Player-anchored snapshot against current bytes",
	                1, 1);
	Con::AddCommand("Lifx", "findPlayer", &FindPlayer,
	                "(int charID) - debug: list every CmCharacterInfo member pointer whose target contains the charID stamp at +0x1B44",
	                2, 2);

	// Player-method variants. The engine passes the Player* as `obj` when
	// these are invoked as `%player.lifxXxx()`. No charID lookup needed.
	// Player-method argc includes argv[1] = object handle. Min=2 = method
	// name + handle (no user args). Max accounts for handle + user args.
	Con::AddCommand("Player", "lifxSnapshotEffects", &SnapshotPlayerThis,
	                "([hex startOff = 0] [, hex endOff = 0x10000]) - snapshot this Player's bytes",
	                2, 4);
	Con::AddCommand("Player", "lifxDiffEffects", &DiffPlayerThis,
	                "() - diff against the last Player snapshot",
	                2, 2);
	Con::AddCommand("Player", "lifxFindEffect", &FindEffectOnPlayer,
	                "(int effectID [, hex startOff] [, hex endOff]) - search for an effect ID inside this Player",
	                3, 5);
	Con::AddCommand("Player", "lifxDumpAt", &DumpAtPlayer,
	                "(hex offset [, byteCount=64]) - hex dump bytes at Player + offset",
	                3, 4);
	Con::AddCommand("Player", "lifxPokeU32", &PokeU32Player,
	                "(hex offset, hex value) - write a uint32 at Player + offset (DEBUG)",
	                4, 4);
	Con::AddCommand("Player", "lifxSetEffectExpiry", &SetEffectExpiry,
	                "(int effectID, int expires_at_ms) - set absolute expires_at on this Player's row for effectID",
	                4, 4);
	Con::AddCommand("Player", "lifxExtendEffect", &ExtendEffect,
	                "(int effectID, int extra_ms) - add extra_ms to this Player's expires_at for effectID",
	                4, 4);
	Con::AddCommand("Player", "lifxClearEffect", &ClearEffect,
	                "(int effectID) - immediately expire this effect on this Player; broadcasts the change so the client HUD refreshes",
	                3, 3);
	Con::AddCommand("Player", "lifxBroadcastEffect", &BroadcastEffectCmd,
	                "(int effectID) - send current row state of effectID to all ghosting clients (no row write); use to test broadcast in isolation",
	                3, 3);
}
