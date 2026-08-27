/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_outpost_proximity.h"
#include "server/cm_server.h"

#include <cstdint>
#include <cstring>

#include <windows.h>

__CM_INSTATNTIATE(_MonumentMinDistanceGetter);
__CM_INSTATNTIATE(_OutpostOutpostMinDistanceGetter);

namespace Hooks { namespace Outpost {

std::atomic<uint32_t> g_monumentMinDistance       {150};
std::atomic<uint32_t> g_outpostOutpostMinDistance {300};
std::atomic<uint32_t> g_outpostToPersonalDistance {20};
std::atomic<uint32_t> g_monumentToPersonalDistance{20};

uint64_t __fastcall MonumentMinDistanceGetter()
{
	return g_monumentMinDistance.load(std::memory_order_relaxed);
}

uint64_t __fastcall OutpostOutpostMinDistanceGetter()
{
	return g_outpostOutpostMinDistance.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Call-site retarget wrappers. Each is a stand-alone function with the same
// `uint64_t __fastcall(void)` signature as the engine helper FUN_140187360
// the original call instruction points at, so swapping the rel32 needs no
// other shim. __declspec(noinline) keeps the address stable in Release
// builds where the compiler would otherwise inline / merge a one-liner.
// ---------------------------------------------------------------------------
namespace {
	extern "C" __declspec(noinline)
	uint64_t __fastcall OutpostToPersonalDistanceWrapper()
	{
		return g_outpostToPersonalDistance.load(std::memory_order_relaxed);
	}

	extern "C" __declspec(noinline)
	uint64_t __fastcall MonumentToPersonalDistanceWrapper()
	{
		return g_monumentToPersonalDistance.load(std::memory_order_relaxed);
	}

	// Rewrite the rel32 of a single `E8 rel32` call instruction so it
	// dispatches to `newTarget` instead of its original destination.
	// Reports the before/after rel32 bytes via Con::Echo. Returns false
	// if the byte at `callSiteRva` is not 0xE8 or the new rel32 doesn't
	// fit in int32 (i.e. our wrapper is somehow further than ±2GB from the
	// patched site, which on a normally-loaded DLL shouldn't happen but
	// we check to fail loud instead of silently miscompiling the call).
	bool PatchCallSite(CmOffset callSiteRva, void* newTarget, const char* label)
	{
		auto* site = reinterpret_cast<uint8_t*>(GetModuleHandle(nullptr))
		           + static_cast<uintptr_t>(callSiteRva);
		if (site[0] != 0xE8) {
			Con::Warning("[lifx-proximity] %s: unexpected call-site opcode — patch ABORTED", label);
			return false;
		}
		const auto callEnd       = reinterpret_cast<uintptr_t>(site) + 5;
		const intptr_t targetRel = reinterpret_cast<intptr_t>(newTarget) - static_cast<intptr_t>(callEnd);
		if (targetRel > INT32_MAX || targetRel < INT32_MIN) {
			Con::Warning("[lifx-proximity] %s: wrapper is >2GB from call site — patch ABORTED", label);
			return false;
		}
		const int32_t newRel = static_cast<int32_t>(targetRel);

		DWORD oldProtect = 0;
		if (!VirtualProtect(site + 1, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
			Con::Warning("[lifx-proximity] %s: VirtualProtect failed (rc=%lu) — patch ABORTED",
			             label, (unsigned long)GetLastError());
			return false;
		}
		std::memcpy(site + 1, &newRel, 4);
		DWORD restored = 0;
		VirtualProtect(site + 1, 4, oldProtect, &restored);
		FlushInstructionCache(GetCurrentProcess(), site, 5);

		Con::Echo("[lifx-proximity] %s: call-site patched", label);
		return true;
	}
}

bool PatchPersonalClaimCallSites()
{
	const bool a = PatchCallSite(CmOffset::CALL_SITE_PERSONAL_LAND_OUTPOST_DIST,
	                             reinterpret_cast<void*>(&OutpostToPersonalDistanceWrapper),
	                             "outpost-vs-personal");
	const bool b = PatchCallSite(CmOffset::CALL_SITE_MONUMENT_PERSONAL_DIST,
	                             reinterpret_cast<void*>(&MonumentToPersonalDistanceWrapper),
	                             "monument-vs-personal");
	return a && b;
}

}} // namespace Hooks::Outpost
