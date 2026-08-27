/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "lifx_timers.h"
#include "server/cm_server.h"
#include "server/api/t3d_console.h"
#include "server/hooks/effect/hook_assign_effect.h"

#include <cstdint>
#include <cstdlib>

namespace
{
	// LifxTimers::resurrection         -> echoes current override (0 = engine default 10min)
	// LifxTimers::resurrection(<ms>)   -> set new duration in ms (0 to restore engine default)
	void Resurrection(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
			const auto ms = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
			const auto prev = Hooks::Effect::g_resurrectionDurationMs.exchange(
				ms, std::memory_order_relaxed);
			Con::Echo("[lifx-timers] resurrection duration: %u ms -> %u ms (0 = engine default 10min)",
			          (unsigned)prev, (unsigned)ms);
			return;
		}
		const auto cur = Hooks::Effect::g_resurrectionDurationMs.load(std::memory_order_relaxed);
		Con::Echo("[lifx-timers] resurrection duration (current override): %u ms (0 = engine default 10min)",
		          (unsigned)cur);
	}

	// LifxTimers::setResurrectionFor(charID, ms) — per-player override.
	// ms = 0 clears the entry; non-zero stores it. Takes precedence over
	// the global LifxTimers::resurrection setting but is overridden by a
	// LifxTimers::onResurrection callback if one is registered.
	void SetResurrectionFor(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 3) {
			Con::Warning("usage: LifxTimers::setResurrectionFor(<charID>, <ms>)");
			return;
		}
		const auto charID = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const auto ms     = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
		Hooks::Effect::SetResurrectionFor(charID, ms);
		Con::Echo("[lifx-timers] per-player resurrection: charID=%u -> %u ms (0 = cleared; %zu override(s) live)",
		          (unsigned)charID, (unsigned)ms,
		          Hooks::Effect::SizeResurrectionOverrides());
	}

	// LifxTimers::getResurrectionFor(charID) — returns the per-player
	// override in ms (0 = not set / using global or engine default).
	S32 GetResurrectionFor(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 2) {
			Con::Warning("usage: LifxTimers::getResurrectionFor(<charID>)");
			return 0;
		}
		const auto charID = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
		return static_cast<S32>(Hooks::Effect::GetResurrectionFor(charID));
	}

	// LifxTimers::clearResurrectionFor(charID) — explicit clear.
	void ClearResurrectionFor(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 2) {
			Con::Warning("usage: LifxTimers::clearResurrectionFor(<charID>)");
			return;
		}
		const auto charID = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
		Hooks::Effect::ClearResurrectionFor(charID);
		Con::Echo("[lifx-timers] per-player resurrection cleared: charID=%u (%zu override(s) live)",
		          (unsigned)charID, Hooks::Effect::SizeResurrectionOverrides());
	}

	// LifxTimers::onResurrection(<scriptFnName>) — registers a TorqueScript
	// function that, on each Resurrected apply, is called as
	//   <fn>(<charID>, <engineDefaultMs>);
	// and is expected to return the desired duration in ms. Pass "" to clear.
	// Highest priority; takes precedence over per-player + global.
	void OnResurrection(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const char* fn = (argc >= 2 && argv[1]) ? argv[1] : "";
		Hooks::Effect::SetResurrectionCallback(fn);
		Con::Echo("[lifx-timers] resurrection callback set to '%s' (empty = cleared)",
		          (fn[0] == '\0') ? "" : fn);
	}
}

void Lifx::Api::Timers::Register()
{
	Con::AddCommand("LifxTimers", "resurrection", &Resurrection,
	                "([int ms]) - get or set global Resurrected duration. 0 = engine default (10 min).",
	                1, 2);
	Con::AddCommand("LifxTimers", "setResurrectionFor", &SetResurrectionFor,
	                "(int charID, int ms) - per-player Resurrected duration override. 0 = clear.",
	                3, 3);
	Con::AddCommand("LifxTimers", "getResurrectionFor", &GetResurrectionFor,
	                "(int charID) - return the per-player override in ms (0 = not set).",
	                2, 2);
	Con::AddCommand("LifxTimers", "clearResurrectionFor", &ClearResurrectionFor,
	                "(int charID) - remove the per-player override.",
	                2, 2);
	Con::AddCommand("LifxTimers", "onResurrection", &OnResurrection,
	                "(string scriptFnName) - register a TorqueScript function called as fn(charID, defaultMs) returning ms. Empty string clears.",
	                1, 2);
}
