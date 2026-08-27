/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_assign_effect.h"
#include "server/api/t3d_console.h"

#include <cstdlib>
#include <mutex>
#include <unordered_map>

__CM_INSTATNTIATE(_ObjEffects_AssignEffect);

// Default 0 (= no override). cm_server.cpp seeds this from lifxpluss.xml
// during Init() before the hook attaches, so by the time the engine boot
// reaches the first effect apply the configured value is already live.
std::atomic<std::uint32_t> Hooks::Effect::g_resurrectionDurationMs{0};

namespace
{
	std::mutex g_perPlayerMu;
	std::unordered_map<std::uint32_t, std::uint32_t> g_perPlayer;

	std::mutex g_callbackMu;
	std::string g_callbackFn;
}

void Hooks::Effect::SetResurrectionFor(std::uint32_t charID, std::uint32_t ms)
{
	std::lock_guard<std::mutex> lk(g_perPlayerMu);
	if (ms == 0) g_perPlayer.erase(charID);
	else         g_perPlayer[charID] = ms;
}

std::uint32_t Hooks::Effect::GetResurrectionFor(std::uint32_t charID)
{
	std::lock_guard<std::mutex> lk(g_perPlayerMu);
	auto it = g_perPlayer.find(charID);
	return it == g_perPlayer.end() ? 0u : it->second;
}

void Hooks::Effect::ClearResurrectionFor(std::uint32_t charID)
{
	std::lock_guard<std::mutex> lk(g_perPlayerMu);
	g_perPlayer.erase(charID);
}

std::size_t Hooks::Effect::SizeResurrectionOverrides()
{
	std::lock_guard<std::mutex> lk(g_perPlayerMu);
	return g_perPlayer.size();
}

void Hooks::Effect::SetResurrectionCallback(const char* fn)
{
	std::lock_guard<std::mutex> lk(g_callbackMu);
	g_callbackFn = (fn == nullptr) ? "" : fn;
}

std::string Hooks::Effect::GetResurrectionCallback()
{
	std::lock_guard<std::mutex> lk(g_callbackMu);
	return g_callbackFn;
}

std::uint32_t Hooks::Effect::ResolveResurrectionMs(std::uint32_t charID,
                                                   std::uint32_t engineDefaultMs)
{
	// 1) TS callback wins. We snapshot the name under lock then release —
	//    Con::Evaluate may itself trigger TS code that re-enters this path.
	std::string fn;
	{
		std::lock_guard<std::mutex> lk(g_callbackMu);
		fn = g_callbackFn;
	}
	if (!fn.empty()) {
		char buf[160];
		std::snprintf(buf, sizeof(buf), "%s(%u, %u);", fn.c_str(),
		              (unsigned)charID, (unsigned)engineDefaultMs);
		const char* result = Con::Evaluate(buf);
		if (result && *result) {
			const auto ms = static_cast<std::uint32_t>(std::strtoul(result, nullptr, 0));
			if (ms > 0) return ms;
		}
	}

	// 2) Explicit per-player override.
	const auto pp = GetResurrectionFor(charID);
	if (pp > 0) return pp;

	// 3) Global default from lifxpluss.xml / LifxTimers::resurrection.
	const auto g = g_resurrectionDurationMs.load(std::memory_order_relaxed);
	if (g > 0) return g;

	// 4) No override — pass through the engine's value.
	return engineDefaultMs;
}

void __fastcall Hooks::Effect::AssignEffect(void* self,
                                            std::uint32_t effectID,
                                            std::uint32_t* row16)
{
	// NOTE: This function is reached only on the receiving side — i.e.
	// clients applying a delta they got from the server. Diagnostic in
	// commit history confirmed Resurrected's server-side apply never
	// flows through here. The hook is kept attached for symmetry/future
	// use but it isn't the duration chokepoint; see hook_broadcast_effects.
	if (effectID == kResurrectedEffectId && row16 != nullptr) {
		const std::uint32_t durationMs = g_resurrectionDurationMs.load(std::memory_order_relaxed);
		if (durationMs != 0) {
			const std::uint32_t expiresAt = row16[0];
			const std::uint32_t appliedAt = row16[1];
			// Only adjust applies (expires > applied). Removals zero expires_at;
			// passing those through unchanged is what lets the engine tear the
			// effect down via its normal path.
			if (expiresAt > appliedAt) {
				const std::uint32_t engineDuration = expiresAt - appliedAt;
				row16[0] = appliedAt + durationMs;
				Con::Echo("[lifx-effect] Resurrected duration override: %u ms -> %u ms (applied_at=%u, new expires_at=%u)",
				          (unsigned)engineDuration, (unsigned)durationMs,
				          (unsigned)appliedAt, (unsigned)row16[0]);
			}
		}
	}
	_ObjEffects_AssignEffect(self, effectID, row16);
}
