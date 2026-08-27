/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "lifx_battlezone.h"
#include "server/cm_server.h"
#include "server/hooks/battlezone/hook_battlezone_containment.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ----------------------------------------------------------------------------
// Engine bindings. Typed views onto addresses inside the running
// cm_yo_server.exe image; none own a detour. RVAs from docs/battlezones.md.
// ----------------------------------------------------------------------------
namespace
{
	template <typename T>
	T* PtrAtRva(U32 rva)
	{
		return reinterpret_cast<T*>(reinterpret_cast<char*>(GetModuleHandle(NULL)) + rva);
	}

	void* LandsManager()
	{
		return *PtrAtRva<void*>(CmOffset::LANDS_MANAGER_SINGLETON);
	}

	// ----- BattleZoneLand layout (0x38 bytes; see docs/battlezones.md) -------
	//   +0x18  rectangle/center sub-object (geoId + radius live here)
	//   +0x30  subtype (u32, the `type` arg to createBattleZone)
	//   +0x34  active flag (u8)
	// id/geoId/radius are read through the same accessors printBattleZones uses.
	constexpr std::size_t BZ_SUBTYPE_OFFSET = 0x30;
	constexpr std::size_t BZ_ACTIVE_OFFSET  = 0x34;
	constexpr std::size_t BZ_GEO_SUBOBJ      = 0x18;
	// Per-type land list head on Lands::Manager (type 4 -> 4*0x40 + 0x1F0).
	constexpr std::size_t BZ_LIST_HEAD_OFFSET = 0x2F0;

	using GetIdFn     = void*    (__fastcall*)(void* /*land*/, void* /*outBuf16*/);
	using GetGeoIdFn  = uint32_t*(__fastcall*)(void* /*geoSub*/, void* /*outBuf*/);
	using GetRadiusFn = uint32_t (__fastcall*)(void* /*geoSub*/);

	GetIdFn     GetIdHolder() { return reinterpret_cast<GetIdFn>(reinterpret_cast<char*>(GetModuleHandle(NULL)) + CmOffset::BATTLEZONE_LAND_GET_ID); }
	GetGeoIdFn  GetGeoIdPtr() { return reinterpret_cast<GetGeoIdFn>(reinterpret_cast<char*>(GetModuleHandle(NULL)) + CmOffset::BATTLEZONE_LAND_GET_GEOID); }
	GetRadiusFn GetRadius()   { return reinterpret_cast<GetRadiusFn>(reinterpret_cast<char*>(GetModuleHandle(NULL)) + CmOffset::BATTLEZONE_LAND_GET_RADIUS); }

	uint32_t BattleZoneId(void* land)
	{
		unsigned char buf[16] = {0};
		void* holder = GetIdHolder()(land, buf);
		if (!holder) return 0;
		return *reinterpret_cast<uint32_t*>(static_cast<char*>(holder) + 4);
	}
	uint32_t BattleZoneGeoId(void* land)
	{
		unsigned char buf[16] = {0};
		uint32_t* p = GetGeoIdPtr()(static_cast<char*>(land) + BZ_GEO_SUBOBJ, buf);
		return p ? *p : 0;
	}
	uint32_t BattleZoneRadius(void* land)
	{
		return GetRadius()(static_cast<char*>(land) + BZ_GEO_SUBOBJ);
	}
	uint32_t BattleZoneType(void* land)
	{
		return *reinterpret_cast<uint32_t*>(static_cast<char*>(land) + BZ_SUBTYPE_OFFSET);
	}
	uint8_t BattleZoneActive(void* land)
	{
		return *reinterpret_cast<uint8_t*>(static_cast<char*>(land) + BZ_ACTIVE_OFFSET);
	}

	// Visit every BattleZoneLand* in the list at LandsManager()+0x2F0.
	// Node layout (from printBattleZones decompile): sentinel = *(mgr+0x2F0);
	// node->next = node[0]; land = node[3]. Iterate until back to sentinel.
	template <typename F>
	void ForEachBattleZone(F&& fn)
	{
		void* mgr = LandsManager();
		if (!mgr) return;
		auto* sentinel = *reinterpret_cast<uint64_t**>(static_cast<char*>(mgr) + BZ_LIST_HEAD_OFFSET);
		if (!sentinel) return;
		// Belt-and-braces cap against a corrupted list.
		int guard = 0;
		for (auto* node = reinterpret_cast<uint64_t*>(sentinel[0]);
		     node && node != sentinel && guard < 100000;
		     node = reinterpret_cast<uint64_t*>(node[0]), ++guard)
		{
			auto* land = reinterpret_cast<void*>(node[3]);
			if (land) fn(land);
		}
	}

	void* FindBattleZoneById(uint32_t landId)
	{
		void* found = nullptr;
		ForEachBattleZone([&](void* land) {
			if (!found && BattleZoneId(land) == landId) found = land;
		});
		return found;
	}

	// Sanitize a zone name for safe interpolation into a Con::Evaluate string.
	std::string SanitizeName(const char* raw)
	{
		std::string out;
		if (!raw) return out;
		for (const char* p = raw; *p && out.size() < 64; ++p) {
			const char c = *p;
			if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-') {
				out.push_back(c);
			}
		}
		return out;
	}
}

// ----------------------------------------------------------------------------
// Command bodies.
// ----------------------------------------------------------------------------
namespace
{
	// Lifx::createBattleZone(type, geoIdInt, radius, name) -> int landId.
	// Invokes the engine's own TS `createBattleZone` (which marshals the name
	// into an engine string for us), then locates the just-created zone and
	// returns its id. The engine command returns void, hence the read-back.
	S32 CreateBattleZone(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 5 || !argv[1] || !argv[2] || !argv[3] || !argv[4]) {
			Con::Warning("usage: Lifx::createBattleZone(<type>, <geoIdInt>, <radius>, <name>)");
			return -1;
		}
		const auto type   = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const auto geoId  = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0));
		const auto radius = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0));
		const std::string name = SanitizeName(argv[4]);
		if (radius == 0) {
			Con::Warning("Lifx::createBattleZone: radius must be > 0");
			return -1;
		}

		char cmd[160];
		std::snprintf(cmd, sizeof(cmd), "createBattleZone(%u, %u, %u, \"%s\");",
		              (unsigned)type, (unsigned)geoId, (unsigned)radius, name.c_str());
		Con::Evaluate(cmd);

		// Read back: among zones at the requested geoId, the new one has the
		// largest id (the engine's id generator is monotonic). Fall back to the
		// max id overall if no geoId matched (defensive).
		uint32_t bestAtGeo = 0; bool haveGeo = false;
		uint32_t bestAny   = 0; bool haveAny = false;
		ForEachBattleZone([&](void* land) {
			const uint32_t id = BattleZoneId(land);
			if (!haveAny || id > bestAny) { bestAny = id; haveAny = true; }
			if (BattleZoneGeoId(land) == geoId && (!haveGeo || id > bestAtGeo)) {
				bestAtGeo = id; haveGeo = true;
			}
		});
		const int32_t newId = haveGeo ? (int32_t)bestAtGeo : (haveAny ? (int32_t)bestAny : -1);
		Con::Echo("Lifx::createBattleZone: type=%u geo=%u radius=%u name=\"%s\" -> landId=%d",
		          (unsigned)type, (unsigned)geoId, (unsigned)radius, name.c_str(), newId);
		return newId;
	}

	void DeleteBattleZone(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 2 || !argv[1]) {
			Con::Warning("usage: Lifx::deleteBattleZone(<landId>)");
			return;
		}
		const auto landId = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		char cmd[64];
		std::snprintf(cmd, sizeof(cmd), "deleteBattleZone(%u);", (unsigned)landId);
		Con::Evaluate(cmd);
		Con::Echo("Lifx::deleteBattleZone: landId=%u", (unsigned)landId);
	}

	void PrintBattleZones(LPVOID /*obj*/, S32 /*argc*/, const char** /*argv*/)
	{
		Con::Evaluate("printBattleZones();");
	}

	// Lifx::getBattleZones() -> "id geo radius type active\n…". printBattleZones
	// only logs to the console; scripts need a value they can parse.
	const char* GetBattleZones(LPVOID /*obj*/, S32 /*argc*/, const char** /*argv*/)
	{
		// Persistent across the return; the engine copies the const char* into a
		// ConsoleValue immediately after we return. thread_local for safety.
		static thread_local std::string out;
		out.clear();
		char row[96];
		ForEachBattleZone([&](void* land) {
			std::snprintf(row, sizeof(row), "%u %u %u %u %u\n",
			              (unsigned)BattleZoneId(land),
			              (unsigned)BattleZoneGeoId(land),
			              (unsigned)BattleZoneRadius(land),
			              (unsigned)BattleZoneType(land),
			              (unsigned)BattleZoneActive(land));
			out += row;
		});
		return out.c_str();
	}

	// Lifx::setBattleZoneActive(landId, active) — arm/disarm containment live
	// without delete+recreate. Arming also forces subtype==1 so the engine's
	// isActiveStartingZone gate passes.
	void SetBattleZoneActive(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 3 || !argv[1] || !argv[2]) {
			Con::Warning("usage: Lifx::setBattleZoneActive(<landId>, <0|1>)");
			return;
		}
		const auto landId = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const bool active = std::strtol(argv[2], nullptr, 0) != 0;
		void* land = FindBattleZoneById(landId);
		if (!land) {
			Con::Warning("Lifx::setBattleZoneActive: no battlezone with landId=%u", (unsigned)landId);
			return;
		}
		if (active) {
			*reinterpret_cast<uint32_t*>(static_cast<char*>(land) + BZ_SUBTYPE_OFFSET) = 1;
		}
		*reinterpret_cast<uint8_t*>(static_cast<char*>(land) + BZ_ACTIVE_OFFSET) = active ? 1 : 0;
		Con::Echo("Lifx::setBattleZoneActive: landId=%u active=%d (subtype=%u)",
		          (unsigned)landId, active ? 1 : 0, (unsigned)BattleZoneType(land));
	}

	void SetBattleZoneExempt(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 3 || !argv[1] || !argv[2]) {
			Con::Warning("usage: Lifx::setBattleZoneExempt(<charId>, <0|1>)");
			return;
		}
		const auto charId = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const bool exempt = std::strtol(argv[2], nullptr, 0) != 0;
		Hooks::BattleZone::SetExempt(charId, exempt);
		Con::Echo("Lifx::setBattleZoneExempt: charId=%u exempt=%d", (unsigned)charId, exempt ? 1 : 0);
	}
}

void Lifx::Api::BattleZone::Register()
{
	Con::AddCommand("Lifx", "createBattleZone", &CreateBattleZone,
	                "(int type, int geoIdInt, int radius, string name) - create a battlezone; returns its landId (-1 on failure). type==1 confines players when active.",
	                5, 5);
	Con::AddCommand("Lifx", "deleteBattleZone", &DeleteBattleZone,
	                "(int landId) - remove a battlezone",
	                2, 2);
	Con::AddCommand("Lifx", "printBattleZones", &PrintBattleZones,
	                "() - log every battlezone to the console",
	                1, 1);
	Con::AddCommand("Lifx", "getBattleZones", &GetBattleZones,
	                "() - return newline-separated rows: id geo radius type active",
	                1, 1);
	Con::AddCommand("Lifx", "setBattleZoneActive", &SetBattleZoneActive,
	                "(int landId, bool active) - arm/disarm containment for an existing zone",
	                3, 3);
	Con::AddCommand("Lifx", "setBattleZoneExempt", &SetBattleZoneExempt,
	                "(int charId, bool exempt) - let a charId pass an armed battlezone boundary",
	                3, 3);
}
