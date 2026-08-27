#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	Hook on Player::BroadcastEffectDelta (RVA 0xEBFF0, vtable slot 187 on
	Player and all NPC classes). This is the chokepoint the server hits
	whenever an effect is added/refreshed/cleared and the change needs to
	go out to ghosting clients.

	Why this and not cObjEffects::Assign_effect (0x4DC810):
	  - Assign_effect has exactly one caller in the binary: the network
	    handler's per-row delta apply (client-side only). The server never
	    invokes it directly — the server writes Player+0x1238 raw and then
	    calls THIS broadcaster.
	  - Therefore hooking Assign_effect would let us shape client-applied
	    deltas but never see a server-initiated apply (which is what
	    Resurrected, AddEffect on respawn, etc. are).

	What the hook does (issue #34):
	  - Each 32-byte delta entry has its effect id at +0 and a 16-byte
	    row block at +8 (expires_at, applied_at, magnitude_data).
	  - For effect id 47 (Resurrected), if the entry looks like an apply
	    (expires > applied) and g_resurrectionDurationMs is non-zero,
	    rewrite the entry's expires_at to applied_at + duration. Also
	    rewrite the matching row in Player+0x1238+47*24 so server gameplay
	    sees the same shortened window.
	  - Pass through unchanged otherwise (removals zero expires_at; we
	    must not touch those or the engine's teardown breaks).
*/

#include "server/cm_server.h"

#include <cstdint>

__CM_DECL_EXTERNAL(bool, __fastcall, _ObjEffects_BroadcastDelta,
                   void* /*player*/, void* /*arg2*/, void* /*deltaList*/);

namespace Hooks
{
	namespace Effect
	{
		bool __fastcall BroadcastDelta(void* player, void* arg2, void* deltaList);

		// 32-byte delta entry as documented in lifx_effects.cpp.
		struct DeltaEntry {
			std::uint32_t effect_id;
			std::uint32_t pad1;
			std::uint32_t expires_at_ms;
			std::uint32_t applied_at_ms;
			std::uint64_t magnitude_data;
			std::uint64_t pad2;
		};
		static_assert(sizeof(DeltaEntry) == 32, "delta entry must be 32 bytes");

		struct DeltaVec {
			DeltaEntry* begin;
			DeltaEntry* end;
			DeltaEntry* end_of_storage;
		};
	}
}
