#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.

	LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
	DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
	DEALINGS IN LIFX.
*  =================================================================================== */

/*
	Battlezone containment hook — selective exemption + snap-back notification.

	Background (see docs/battlezones.md): an "active starting zone"
	(BattleZoneLand, handle tag 4, subtype==1, active byte +0x34 set) confines
	players. Player::_checkSteps (RVA 0xF0500) calls
	Lands::Manager::isActiveStartingZone (RVA 0x2D78C0) on the player's current
	zone; when it returns true the engine snaps a stepping-out player back to
	their previous tile via Player::teleportTo. The gate is the single decision
	point, and isActiveStartingZone has exactly ONE caller (_checkSteps).

	The gate takes (Manager*, landHandle) — it does NOT receive the player, so
	we cannot key a per-player exemption inside it alone. Two cooperating
	detours solve that:

	  1. _checkSteps detour — a thin trampoline that stamps the Player* being
	     processed into g_currentCheckPlayer, calls the original, then clears.
	     The sim runs single-threaded so a plain atomic pointer is enough.

	  2. isActiveStartingZone detour — calls the original; if it says "contain",
	     it reads g_currentCheckPlayer's charID (Player+0x1B44). If that charID
	     is on the exempt set, it returns false (player passes through).
	     Otherwise it fires a throttled TorqueScript callback
	     `LifxBattleZoneOnContained(<charId>)` so the script layer can show the
	     "Return to fight!" message, then returns true (containment proceeds).

	Doing the message via a TS callback (rather than rebuilding the engine's
	CmMessage-send path in C++) keeps the hot move path simple and lets the
	message id / wording live in script.
*/

#include "server/cm_server.h"

#include <cstdint>

__CM_DECL_EXTERNAL(void,     __fastcall, _Player_CheckSteps,          void* /*player*/);
__CM_DECL_EXTERNAL(uint64_t, __fastcall, _Lands_IsActiveStartingZone, void* /*mgr*/, uint64_t /*landHandle*/);

namespace Hooks
{
	namespace BattleZone
	{
		void     __fastcall CheckStepsCall(void* player);
		uint64_t __fastcall IsActiveStartingZoneCall(void* mgr, uint64_t landHandle);

		// Per-charID exemption set, driven by Lifx::setBattleZoneExempt.
		void SetExempt(uint32_t charId, bool exempt);
		bool IsExempt(uint32_t charId);
	}
}
