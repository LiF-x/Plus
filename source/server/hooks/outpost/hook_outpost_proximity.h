#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	Four runtime knobs over the engine's claim-proximity thresholds.

	The first two are standard "constant-returner function" detours, same
	pattern as Hooks::Outpost::DefaultRadiusGetter — the engine helper is a
	one-instruction `return N;` and we serve our own atomic instead.

	The last two are a different mechanism: their underlying constant is
	`FUN_140187360()` (the 20-default getter), which is shared across many
	call sites with different semantics. We can't redirect it globally
	without also moving the radius default. Instead we byte-patch the rel32
	of two specific `E8 …` call instructions so they jump to wrapper
	functions inside this DLL — leaving every other call site to the same
	getter untouched.

	Patch sites (verified via Ghidra; bytes documented next to each in
	cm_offsets.h):

	    0x2D1C65  -> outpost ↔ personal claim min-distance (cm_messages 748)
	    0x2CFE45  -> guild monument ↔ personal claim min-distance (cm_messages 756)

	None of the four mechanisms here interact: setting one knob never
	moves another.
*/

#include "server/cm_server.h"

#include <atomic>
#include <cstdint>

__CM_DECL_EXTERNAL(uint64_t, __fastcall, _MonumentMinDistanceGetter, void);
__CM_DECL_EXTERNAL(uint64_t, __fastcall, _OutpostOutpostMinDistanceGetter, void);

namespace Hooks
{
	namespace Outpost
	{
		uint64_t __fastcall MonumentMinDistanceGetter();
		uint64_t __fastcall OutpostOutpostMinDistanceGetter();

		extern std::atomic<uint32_t> g_monumentMinDistance;        // default 150
		extern std::atomic<uint32_t> g_outpostOutpostMinDistance;  // default 300
		extern std::atomic<uint32_t> g_outpostToPersonalDistance;  // default 20  (call-site retarget)
		extern std::atomic<uint32_t> g_monumentToPersonalDistance; // default 20  (call-site retarget)

		// Rewrite the rel32 of the two `E8 …` instructions documented above
		// so each calls into one of the wrapper functions below. Returns
		// false (and logs via Con::Warning) if either call site is not the
		// expected 5-byte `E8 rel32` we read at reverse-engineering time —
		// any failure means our offsets are stale.
		bool PatchPersonalClaimCallSites();
	}
}
