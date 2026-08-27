#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	Hook on FUN_140090F60 — the suspected "apply damage to live HP + broadcast"
	function called from Player::_applyHit after dealDamage.

	From the Player::_applyHit decompile (see docs/character_hp.md and
	/tmp/lifx_ghidra/decompile/dd_EE0F0.c around line 601):

	    FUN_140090f60(plVar3, &local_568);

	where:
	    plVar3      = param_1 + 0x155  — the Player's character-stats sub-object
	    &local_568  = pointer to a damage packet whose first 16 bytes are
	                  the hard/soft HP damage (int64 each, ×1e6 scale) filled
	                  by ONEPUNCHMAN one call earlier.

	Suspected signature:
	    void __fastcall apply_damage(void* charStats, void* dmgPacket);

	Telemetry plan:
	  - Log entry with both args + the first 4 int64 fields of dmgPacket.
	  - Capture g_lastCharStats so a Lifx command can re-invoke this
	    function to apply arbitrary damage to ourselves on demand.
	  - Mutate dmgPacket BEFORE calling the original if a staged override
	    is set, so we can verify our damage values reach the engine.
*/

#include "server/cm_server.h"

#include <atomic>

__CM_DECL_EXTERNAL(void, __fastcall, _Hit_ApplyDamage,
                   void* charStats, void* dmgPacket);

namespace Hooks
{
	namespace HitApplyDamage
	{
		void Call(void* charStats, void* dmgPacket);

		extern std::atomic<unsigned long long> g_callCount;
		extern void* g_lastCharStats;
		extern void* g_lastDmgPacket;

		// Staged override applied to the next packet that flows through.
		extern std::atomic<bool> g_pendingOverride;
		extern long long g_overrideHardHp;
		extern long long g_overrideSoftHp;
	}
}
