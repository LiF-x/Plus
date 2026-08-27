/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).
*  =================================================================================== */

#include "hook_naked_render.h"
#include "hook_console.h"          // _Engine_Con_InternalConsolePrintf (for diag logging)
#include "client/client_offsets.h"
#include "lifx_armor_meshes.h"     // kLifxArmorMeshes / kLifxArmorMeshCount (generated)

#include <cstdint>
#include <cstdio>
#include <unordered_set>

__LIFX_INSTANTIATE(_Engine_QueryRenderObjects);
__LIFX_INSTANTIATE(_Engine_SetMeshHidden);
__LIFX_INSTANTIATE(_Engine_TmpHideAllNaked);

namespace
{
	// The query fills a std::vector<RenderEntry> { begin, end, cap }. Each entry
	// is 16 bytes; the object pointer is the first qword (matches the disasm of
	// tmpHideAllNakedMans: `mov rbp,[rbx]; ... rbx += 0x10`).
	struct QueryVec { char* begin; char* end; char* cap; };
	constexpr unsigned kEntryStride   = 0x10;
	constexpr unsigned kShapeReadyOff = 0x10;     // [obj+0x10] flags
	constexpr std::uint32_t kShapeReady = 0x400;  // shape-ready bit
	constexpr unsigned kTypeMaskOff   = 0x1b0;    // [obj+0x1b0] typemask (query AND-filters on this)
	constexpr unsigned kMeshCountOff  = 0x127c;   // [obj+0x127c] shape mesh count (from setMeshHidden disasm)
	// Diagnostic-confirmed across two runs: the player-model NPCDecorative carries
	// typemask bit 0x10000 (an NPC/ghost marker); the local control player does NOT
	// (it was 0x0000ac80 vs the NPCs' 0x0001ac80). So we strip objects that HAVE this
	// bit (the NPCs) and skip the one without it (the GM's own controlled player).
	constexpr std::uint32_t kNpcMarkerBit = 0x10000;

	void diag(const char* fmt, ...)
	{
		if (!_Engine_Con_InternalConsolePrintf) return;
		char buf[256];
		va_list ap; va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		_Engine_Con_InternalConsolePrintf(0, 0, buf);
	}

	std::uint32_t rd32(void* obj, unsigned off)
	{
		return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(obj) + off);
	}
}

// Replaces tmpHideAllNakedMans. DIAGNOSTIC PASS (issue #125): the stock command
// queries renderable objects with typemask 0x8000 (real players) and our NPC may
// not carry that bit, so it's never touched. We log what each mask returns so we
// can identify the NPCDecorative's typemask, while still hiding armor on the safe
// 0x8000 set (no behaviour regression, no crash risk -- the broad query is
// log-only and never calls setMeshHidden).
void __fastcall LifxClient::HookNakedRender::OnHideAllNaked(void* /*obj*/, int /*argc*/, const char** /*argv*/)
{
	void* container = reinterpret_cast<char*>(GetModuleHandle(NULL)) + ClientOffset::CLIENT_RENDER_CONTAINER;

	// This runs on a ~750ms client tick (lifxEquipTick) so it auto-applies to
	// NPCs as they scope in. setMeshHidden is idempotent, so re-applying every
	// tick is cheap and self-heals if the engine re-shows a mesh. Log only the
	// FIRST time we touch a given object so the console isn't spammed per tick.
	static std::unordered_set<void*> g_loggedObjs;

	QueryVec vec{ nullptr, nullptr, nullptr };
	_Engine_QueryRenderObjects(container, 0x8000, &vec);

	for (char* e = vec.begin; e && e < vec.end; e += kEntryStride) {
		void* o = *reinterpret_cast<void**>(e);
		if (!o) continue;
		std::uint32_t tm = rd32(o, kTypeMaskOff);
		bool ready = (rd32(o, kShapeReadyOff) & kShapeReady) != 0;
		const bool firstSeen = g_loggedObjs.find(o) == g_loggedObjs.end();

		// Only strip player-model NPCs (0x10000 marker); leave real players clothed.
		if (!(tm & kNpcMarkerBit))
			continue;                          // a real player — silently skip every tick
		if (!ready)
			continue;                          // shape not built yet — try again next tick

		// Hide every armor mesh first (clean naked base)...
		for (unsigned i = 0; i < kLifxArmorMeshCount; ++i)
			_Engine_SetMeshHidden(o, kLifxArmorMeshes[i], 1);
		// ...then un-hide the equipped subset so the NPC renders WEARING it.
		// A2a equip via the commandToClient channel (replaces the retired
		// ghost-piggyback transport). Hardcoded full-plate set for now (indices
		// into kLifxArmorMeshes); 2b will pass the NPC's real loadout as an arg.
		static const unsigned kEquipSet[] = { 88, 89, 90, 91, 92, 94 }; // Full_Plate_* (omit 93 visor-add)
		for (unsigned k = 0; k < sizeof(kEquipSet) / sizeof(kEquipSet[0]); ++k) {
			if (kEquipSet[k] < kLifxArmorMeshCount)
				_Engine_SetMeshHidden(o, kLifxArmorMeshes[kEquipSet[k]], 0);
		}
		if (firstSeen) {                       // log once per object, not per tick
			g_loggedObjs.insert(o);
			diag("LIFX-EQUIP: auto-equipped NPC obj=%p typemask=0x%08x (%u meshes)\n",
			     o, tm, (unsigned)(sizeof(kEquipSet) / sizeof(kEquipSet[0])));
		}
	}
	// Intentionally do NOT free vec.begin: a mismatched free would crash and a
	// small per-tick leak is acceptable for this dev sweep.
}
