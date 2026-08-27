/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_broadcast_effects.h"
#include "hook_assign_effect.h"
#include "server/api/t3d_console.h"

#include <cstddef>

__CM_INSTATNTIATE(_ObjEffects_BroadcastDelta);

namespace
{
	// Mirrors lifx_effects.cpp — table is 24-byte rows indexed by effect id.
	constexpr std::size_t kPlayerEffectTableBase = 0x1238;
	constexpr std::size_t kPlayerEffectRowSize   = 24;
	// Player + 0x1B44 = charID (see engine_internals.h).
	constexpr std::size_t kCharIdOffOnPlayer     = 0x1B44;
}

bool __fastcall Hooks::Effect::BroadcastDelta(void* player, void* arg2, void* deltaList)
{
	if (player != nullptr && deltaList != nullptr) {
		auto* vec = static_cast<DeltaVec*>(deltaList);
		auto* p = vec->begin;
		auto* end = vec->end;
		const auto charID = *reinterpret_cast<std::uint32_t*>(
			static_cast<unsigned char*>(player) + kCharIdOffOnPlayer);
		for (; p && p < end; ++p) {
			if (p->effect_id != Hooks::Effect::kResurrectedEffectId) continue;
			// Only adjust applies — removals (expires==0) and stale heartbeats pass through.
			if (p->expires_at_ms <= p->applied_at_ms) continue;
			const std::uint32_t engineDuration = p->expires_at_ms - p->applied_at_ms;
			const std::uint32_t durationMs = Hooks::Effect::ResolveResurrectionMs(charID, engineDuration);
			if (durationMs == engineDuration) continue; // nothing to do

			const std::uint32_t oldExpires = p->expires_at_ms;
			const std::uint32_t newExpires = p->applied_at_ms + durationMs;
			p->expires_at_ms = newExpires;

			// Mirror the change into Player+0x1238 row 47 so server-side ticks
			// see the same shortened window.
			auto* row = static_cast<unsigned char*>(player)
			          + kPlayerEffectTableBase + p->effect_id * kPlayerEffectRowSize;
			const auto stamp = *reinterpret_cast<std::uint32_t*>(row + 8);
			if (stamp == p->effect_id) {
				*reinterpret_cast<std::uint32_t*>(row + 0) = newExpires;
			}

			Con::Echo("[lifx-effect] Resurrected override charID=%u engine=%u ms override=%u ms expires_at %u -> %u",
			          (unsigned)charID,
			          (unsigned)engineDuration, (unsigned)durationMs,
			          (unsigned)oldExpires, (unsigned)newExpires);
		}
	}
	return _ObjEffects_BroadcastDelta(player, arg2, deltaList);
}
