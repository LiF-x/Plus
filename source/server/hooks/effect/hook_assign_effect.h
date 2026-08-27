#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	Hook on cObjEffects::Assign_effect (RVA 0x4DC810) — engine's per-row
	apply/remove sink for the effect manager. Used to enforce a configurable
	duration on effect id 47 (Resurrected), dropping the hardcoded 10 minutes
	to whatever lifxpluss.xml's <resurrectionDurationMs> says (default 5 min).

	Engine signature, per Ghidra decompile:
		void __fastcall Assign_effect(cObjEffects* this, uint32_t effectID, uint32_t* row16)
	where row16 is a 16-byte block laid out as:
		[0..4]   u32 expires_at_ms   (absolute global clock)
		[4..8]   u32 applied_at_ms
		[8..16]  u64 magnitude_data  (mirrors row+16 of the Player+0x1238 table)

	Override rule: if effectID == 47 and the row looks like an apply
	(expires_at > applied_at), rewrite expires_at = applied_at + g_durationMs.
	Set g_durationMs to 0 to leave the engine default untouched.
*/

#include "server/cm_server.h"

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string>

__CM_DECL_EXTERNAL(void, __fastcall, _ObjEffects_AssignEffect, void* /*this*/, std::uint32_t /*effectID*/, std::uint32_t* /*row16*/);

namespace Hooks
{
	namespace Effect
	{
		void __fastcall AssignEffect(void* self, std::uint32_t effectID, std::uint32_t* row16);

		// 0 = leave engine default (10 min); otherwise the duration in ms
		// the hook rewrites the row to for Resurrected applies.
		extern std::atomic<std::uint32_t> g_resurrectionDurationMs;

		// Effect id we intercept; named only for clarity in the cpp.
		constexpr std::uint32_t kResurrectedEffectId = 47;

		// Per-player Resurrected duration overrides. Set via
		// LifxTimers::setResurrectionFor(charID, ms); ms=0 removes the entry.
		// Takes precedence over the global g_resurrectionDurationMs.
		void SetResurrectionFor(std::uint32_t charID, std::uint32_t ms);
		std::uint32_t GetResurrectionFor(std::uint32_t charID);
		void ClearResurrectionFor(std::uint32_t charID);
		std::size_t SizeResurrectionOverrides();

		// TorqueScript callback registration. The callback is invoked from
		// the broadcast hook as `<fn>(<charID>, <engineDefaultMs>);` and is
		// expected to return the duration in ms (numeric). Empty string clears.
		// Takes highest precedence over per-player and global.
		void SetResurrectionCallback(const char* fn);
		std::string GetResurrectionCallback();

		// Combined resolver. Returns the effective Resurrected duration for
		// this player: callback (if set) -> per-player -> global -> default.
		std::uint32_t ResolveResurrectionMs(std::uint32_t charID, std::uint32_t engineDefaultMs);
	}
}
