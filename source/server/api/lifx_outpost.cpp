/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "lifx_outpost.h"
#include "server/cm_server.h"
#include "server/hooks/outpost/hook_outpost_default_radius.h"
#include "server/hooks/outpost/hook_outpost_proximity.h"

#include <cstdint>
#include <cstdlib>

// ----------------------------------------------------------------------------
// Engine bindings — singletons + raw function pointers reached through
// CmOffsets. None of these own a detour; they're just typed views onto
// addresses inside the running cm_yo_server.exe image. See cm_offsets.h for
// the cross-reference evidence for each address.
// ----------------------------------------------------------------------------
namespace
{
	template <typename T>
	T* PtrAtRva(U32 rva)
	{
		return reinterpret_cast<T*>(reinterpret_cast<char*>(GetModuleHandle(NULL)) + rva);
	}

	// Both managers live in .data as plain `Manager*` slots populated during
	// engine init; we read them every call rather than caching so a server
	// restart inside the same DLL load doesn't leave us with a stale pointer.
	void* LandsManager()
	{
		return *PtrAtRva<void*>(CmOffset::LANDS_MANAGER_SINGLETON);
	}

	void* OutpostsManager()
	{
		return *PtrAtRva<void*>(CmOffset::OUTPOSTS_MANAGER_SINGLETON);
	}

	using ChangeGuildLandRadiusFn = uint64_t(__fastcall*)(void* /*mgr*/, uint64_t /*landRef*/, uint32_t /*newRadius*/);
	using SetProductionTypeFn     = void   (__fastcall*)(void* /*outpost*/, int /*typeID*/);

	ChangeGuildLandRadiusFn ChangeGuildLandRadius()
	{
		return reinterpret_cast<ChangeGuildLandRadiusFn>(
			reinterpret_cast<char*>(GetModuleHandle(NULL)) + CmOffset::OUTPOST_CHANGE_GUILD_LAND_RADIUS);
	}

	SetProductionTypeFn SetProductionType()
	{
		return reinterpret_cast<SetProductionTypeFn>(
			reinterpret_cast<char*>(GetModuleHandle(NULL)) + CmOffset::OUTPOST_SET_PRODUCTION_TYPE);
	}

	// ----- Outpost in-memory record layout (see docs/outposts.md) ---------
	// Derived from full reverse of Outposts::Outpost::maintenance and the
	// recalcSlaves tail block (which writes +0x10 from the production formula).
	//   +0x00  UnmovableObjectID  (u32)
	//   +0x08  outpostType        (u32, 1702..1711 — the key into DAT_140AD20B0)
	//   +0x10  perTickCount       (u32, recomputed every recalcSlaves pass)
	//   +0x14  ProductionObjectTypeID (u32, the field setProductionType writes)
	//   +0x18  currentQuality     (u32-widened u16)
	//   +0x1C  damagedFlag        (u8)
	// OwnerGuildID does NOT live on the Outpost — it's in guild_lands keyed
	// by UnmovableObjectID. Resolving it would need a Lands::Manager
	// cross-reference per row; out of scope for this dump.
	//
	// Container on Outposts::Manager:
	//   +0x20  std::list<Outpost*> sentinel head (MSVC layout: { next, prev, value })
	//   +0x28  size (u64)
	uint32_t OutpostUnmovableID(const void* outpost)
	{
		return *static_cast<const uint32_t*>(outpost);
	}
	uint32_t OutpostType(const void* outpost)
	{
		return *reinterpret_cast<const uint32_t*>(static_cast<const char*>(outpost) + 0x08);
	}
	uint32_t OutpostPerTickCount(const void* outpost)
	{
		return *reinterpret_cast<const uint32_t*>(static_cast<const char*>(outpost) + 0x10);
	}
	uint32_t OutpostProductionTypeID(const void* outpost)
	{
		return *reinterpret_cast<const uint32_t*>(static_cast<const char*>(outpost) + 0x14);
	}
	uint32_t OutpostQuality(const void* outpost)
	{
		return *reinterpret_cast<const uint32_t*>(static_cast<const char*>(outpost) + 0x18);
	}

	// Visit every Outpost* hanging off the std::list at OutpostsManager + 0x20.
	template <typename F>
	void ForEachOutpost(F&& fn)
	{
		void* mgr = OutpostsManager();
		if (!mgr) return;
		auto* head = *reinterpret_cast<uint64_t**>(static_cast<char*>(mgr) + 0x20);
		if (!head) return;
		// MSVC std::list sentinel: head[0]=next, head[1]=prev, head[2]=value.
		// Real elements follow the same shape; iterate until we hit the
		// sentinel again. Cap at the stored size as a belt-and-braces guard
		// against a corrupted list flying us off the rails.
		const uint64_t capacity = *reinterpret_cast<const uint64_t*>(static_cast<char*>(mgr) + 0x28);
		uint64_t seen = 0;
		for (auto* node = reinterpret_cast<uint64_t*>(head[0]); node && node != head && seen < capacity; ++seen) {
			auto* outpost = reinterpret_cast<void*>(node[2]);
			if (outpost) {
				fn(outpost);
			}
			node = reinterpret_cast<uint64_t*>(node[0]);
		}
	}

	// Linear scan by UnmovableObjectID. We have a fast map at OutpostsManager+0x18
	// keyed by ComplexObjectID, but constructing the right ComplexObjectID
	// from a bare UnmovableObjectID would need another engine helper we
	// haven't bound. Total outpost count on a populated server is in the
	// low hundreds; O(n) is fine.
	void* FindOutpostByUnmovableID(uint32_t unmovableID)
	{
		void* found = nullptr;
		ForEachOutpost([&](void* o) {
			if (!found && OutpostUnmovableID(o) == unmovableID) {
				found = o;
			}
		});
		return found;
	}
}

// ----------------------------------------------------------------------------
// Command bodies (one per Lifx::* TorqueScript entry point).
// ----------------------------------------------------------------------------
namespace
{
	S32 GetOutpostDefaultRadius(LPVOID /*obj*/, S32 /*argc*/, const char* /*argv*/[])
	{
		return static_cast<S32>(Hooks::Outpost::g_defaultRadius.load(std::memory_order_relaxed));
	}

	void SetOutpostDefaultRadius(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 2 || !argv[1]) {
			Con::Warning("usage: Lifx::setOutpostDefaultRadius(<radius>)");
			return;
		}
		const auto v = std::strtoul(argv[1], nullptr, 0);
		if (v == 0) {
			Con::Warning("Lifx::setOutpostDefaultRadius: radius must be > 0");
			return;
		}
		const auto prev = Hooks::Outpost::g_defaultRadius.exchange(
			static_cast<uint32_t>(v), std::memory_order_relaxed);
		Con::Echo("Lifx::setOutpostDefaultRadius: %u -> %u (applies to new outposts)",
		          (unsigned)prev, (unsigned)v);
	}

	// Lifx::setOutpostRadius(landID, newRadius) — wraps
	// Lands::Manager::changeGuildLandRadius for already-existing outposts.
	// Engine handles persistence (UPDATE guild_lands SET Radius=…) and the
	// monument rebuild for us.
	void SetOutpostRadius(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 3 || !argv[1] || !argv[2]) {
			Con::Warning("usage: Lifx::setOutpostRadius(<guildLandID>, <newRadius>)");
			return;
		}
		const auto landID    = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const auto newRadius = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 0));
		if (newRadius == 0) {
			Con::Warning("Lifx::setOutpostRadius: radius must be > 0");
			return;
		}
		void* mgr = LandsManager();
		if (!mgr) {
			Con::Warning("Lifx::setOutpostRadius: Lands::Manager singleton is null (engine not initialised?)");
			return;
		}
		// Tagged handle: low byte = land type discriminator (0x01 = outpost),
		// upper 32 bits = guild_lands.ID. The engine rejects any other tag
		// with "wrong land type" so this conveniently doubles as a safety
		// net against passing a personal-land ID by mistake.
		const uint64_t landRef = (static_cast<uint64_t>(landID) << 32) | 0x01ULL;
		ChangeGuildLandRadius()(mgr, landRef, newRadius);
		Con::Echo("Lifx::setOutpostRadius: landID=%u -> radius=%u (engine persists + rebuilds monument)",
		          landID, newRadius);
	}

	// Lifx::setOutpostProductionType(unmovableObjectID, typeID) — looks up
	// the live Outpost record by its UnmovableObjectID, calls the engine's
	// own setter so the UPDATE outposts SET ProductionObjectTypeID… fires.
	void SetOutpostProductionType(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 3 || !argv[1] || !argv[2]) {
			Con::Warning("usage: Lifx::setOutpostProductionType(<unmovableObjectID>, <typeID>)");
			return;
		}
		const auto unmovableID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const auto typeID      = static_cast<int>(std::strtol(argv[2], nullptr, 0));
		void* outpost = FindOutpostByUnmovableID(unmovableID);
		if (!outpost) {
			Con::Warning("Lifx::setOutpostProductionType: no outpost with UnmovableObjectID=%u",
			             unmovableID);
			return;
		}
		const auto before = OutpostProductionTypeID(outpost);
		SetProductionType()(outpost, typeID);
		Con::Echo("Lifx::setOutpostProductionType: UnmovableObjectID=%u  ProductionObjectTypeID  %u -> %d",
		          unmovableID, before, typeID);
	}

	// ---- Proximity knobs --------------------------------------------------
	// Tiny shared helper for the four "set a single positive uint32" knobs.
	// Returns true if a value was stored; logs warnings on usage errors.
	bool SetU32Atomic(std::atomic<uint32_t>& slot,
	                  const char* commandName,
	                  S32 argc, const char* argv[])
	{
		if (argc < 2 || !argv[1]) {
			Con::Warning("usage: %s(<distance>)", commandName);
			return false;
		}
		const auto v = std::strtoul(argv[1], nullptr, 0);
		if (v == 0) {
			Con::Warning("%s: distance must be > 0", commandName);
			return false;
		}
		const auto prev = slot.exchange(static_cast<uint32_t>(v), std::memory_order_relaxed);
		Con::Echo("%s: %u -> %u", commandName, (unsigned)prev, (unsigned)v);
		return true;
	}

	S32 GetMonumentMinDistance(LPVOID, S32, const char*[])
	{
		return static_cast<S32>(Hooks::Outpost::g_monumentMinDistance.load(std::memory_order_relaxed));
	}
	void SetMonumentMinDistance(LPVOID, S32 argc, const char* argv[])
	{
		SetU32Atomic(Hooks::Outpost::g_monumentMinDistance,
		             "Lifx::setMonumentMinDistance", argc, argv);
	}

	S32 GetOutpostOutpostMinDistance(LPVOID, S32, const char*[])
	{
		return static_cast<S32>(Hooks::Outpost::g_outpostOutpostMinDistance.load(std::memory_order_relaxed));
	}
	void SetOutpostOutpostMinDistance(LPVOID, S32 argc, const char* argv[])
	{
		SetU32Atomic(Hooks::Outpost::g_outpostOutpostMinDistance,
		             "Lifx::setOutpostOutpostMinDistance", argc, argv);
	}

	S32 GetOutpostMinDistanceToPersonalClaim(LPVOID, S32, const char*[])
	{
		return static_cast<S32>(Hooks::Outpost::g_outpostToPersonalDistance.load(std::memory_order_relaxed));
	}
	void SetOutpostMinDistanceToPersonalClaim(LPVOID, S32 argc, const char* argv[])
	{
		SetU32Atomic(Hooks::Outpost::g_outpostToPersonalDistance,
		             "Lifx::setOutpostMinDistanceToPersonalClaim", argc, argv);
	}

	S32 GetMonumentMinDistanceToPersonalClaim(LPVOID, S32, const char*[])
	{
		return static_cast<S32>(Hooks::Outpost::g_monumentToPersonalDistance.load(std::memory_order_relaxed));
	}
	void SetMonumentMinDistanceToPersonalClaim(LPVOID, S32 argc, const char* argv[])
	{
		SetU32Atomic(Hooks::Outpost::g_monumentToPersonalDistance,
		             "Lifx::setMonumentMinDistanceToPersonalClaim", argc, argv);
	}

	void DumpOutposts(LPVOID /*obj*/, S32 /*argc*/, const char* /*argv*/[])
	{
		void* mgr = OutpostsManager();
		if (!mgr) {
			Con::Echo("Lifx::dumpOutposts: Outposts::Manager singleton is null");
			return;
		}
		const auto size = *reinterpret_cast<const uint64_t*>(static_cast<char*>(mgr) + 0x28);
		Con::Echo("Lifx::dumpOutposts: %llu outpost(s)", (unsigned long long)size);
		Con::Echo("  UnmovableObjectID  outpostType  perTickCount  ProductionObjectTypeID  currentQuality");
		ForEachOutpost([](void* o) {
			Con::Echo("  %17u  %11u  %12u  %22u  %14u",
			          OutpostUnmovableID(o),
			          OutpostType(o),
			          OutpostPerTickCount(o),
			          OutpostProductionTypeID(o),
			          OutpostQuality(o));
		});
	}
}

void Lifx::Api::Outpost::Register()
{
	Con::AddCommand("Lifx", "getOutpostDefaultRadius", &GetOutpostDefaultRadius,
	                "() - returns the current default radius applied to newly-built outposts",
	                1, 1);
	Con::AddCommand("Lifx", "setOutpostDefaultRadius", &SetOutpostDefaultRadius,
	                "(int radius) - override the default radius applied to newly-built outposts (engine baseline: 20)",
	                2, 2);
	Con::AddCommand("Lifx", "setOutpostRadius", &SetOutpostRadius,
	                "(int guildLandID, int newRadius) - change an EXISTING outpost's influence radius. Engine persists + rebuilds monument.",
	                3, 3);
	Con::AddCommand("Lifx", "setOutpostProductionType", &SetOutpostProductionType,
	                "(int unmovableObjectID, int typeID) - retarget an existing outpost's production output to typeID",
	                3, 3);
	Con::AddCommand("Lifx", "dumpOutposts", &DumpOutposts,
	                "() - list every live outpost: UnmovableObjectID, outpostType, perTickCount, ProductionObjectTypeID, currentQuality",
	                1, 1);

	// Claim-proximity knobs. See docs/outposts.md "Proximity knobs" for the
	// underlying RVAs and which placement check each constant gates.
	Con::AddCommand("Lifx", "getMonumentMinDistance", &GetMonumentMinDistance,
	                "() - current guild monument <-> guild monument min distance (engine baseline: 150)",
	                1, 1);
	Con::AddCommand("Lifx", "setMonumentMinDistance", &SetMonumentMinDistance,
	                "(int distance) - override guild monument <-> guild monument min distance",
	                2, 2);
	Con::AddCommand("Lifx", "getOutpostOutpostMinDistance", &GetOutpostOutpostMinDistance,
	                "() - current outpost <-> outpost/guild-land min distance (engine baseline: 300)",
	                1, 1);
	Con::AddCommand("Lifx", "setOutpostOutpostMinDistance", &SetOutpostOutpostMinDistance,
	                "(int distance) - override outpost <-> outpost/guild-land min distance",
	                2, 2);
	Con::AddCommand("Lifx", "getOutpostMinDistanceToPersonalClaim", &GetOutpostMinDistanceToPersonalClaim,
	                "() - current outpost <-> personal claim min distance (engine baseline: 20, independent of default radius)",
	                1, 1);
	Con::AddCommand("Lifx", "setOutpostMinDistanceToPersonalClaim", &SetOutpostMinDistanceToPersonalClaim,
	                "(int distance) - override outpost <-> personal claim min distance",
	                2, 2);
	Con::AddCommand("Lifx", "getMonumentMinDistanceToPersonalClaim", &GetMonumentMinDistanceToPersonalClaim,
	                "() - current guild monument <-> personal claim min distance (engine baseline: 20)",
	                1, 1);
	Con::AddCommand("Lifx", "setMonumentMinDistanceToPersonalClaim", &SetMonumentMinDistanceToPersonalClaim,
	                "(int distance) - override guild monument <-> personal claim min distance",
	                2, 2);
}
