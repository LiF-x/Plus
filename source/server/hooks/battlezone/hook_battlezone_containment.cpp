/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_battlezone_containment.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

__CM_INSTATNTIATE(_Player_CheckSteps);
__CM_INSTATNTIATE(_Lands_IsActiveStartingZone);

namespace
{
	// Player* currently being processed by _checkSteps. The engine sim is
	// single-threaded, but we use an atomic so a torn read is impossible if
	// that ever changes. Cleared back to null on exit so a stray
	// isActiveStartingZone call from anywhere else can't misattribute.
	std::atomic<void*> g_currentCheckPlayer{nullptr};

	// charID lives at Player+0x1B44 (seen in the _checkSteps decompile).
	constexpr std::size_t PLAYER_CHARID_OFFSET = 0x1B44;

	uint32_t PlayerCharId(void* player)
	{
		if (!player) return 0;
		return *reinterpret_cast<uint32_t*>(static_cast<char*>(player) + PLAYER_CHARID_OFFSET);
	}

	std::mutex                    g_exemptGuard;
	std::unordered_set<uint32_t>  g_exempt;

	// Throttle the TS notification so we don't evaluate a script call on every
	// blocked step (the player rubber-bands several times a second at a wall).
	std::mutex                         g_notifyGuard;
	std::unordered_map<uint32_t, uint64_t> g_lastNotifyMs;
	constexpr uint64_t NOTIFY_THROTTLE_MS = 1500;

	bool ShouldNotify(uint32_t charId)
	{
		const uint64_t now = GetTickCount64();
		std::lock_guard<std::mutex> lk(g_notifyGuard);
		auto it = g_lastNotifyMs.find(charId);
		if (it != g_lastNotifyMs.end() && (now - it->second) < NOTIFY_THROTTLE_MS) {
			return false;
		}
		g_lastNotifyMs[charId] = now;
		return true;
	}
}

void Hooks::BattleZone::SetExempt(uint32_t charId, bool exempt)
{
	std::lock_guard<std::mutex> lk(g_exemptGuard);
	if (exempt) g_exempt.insert(charId);
	else        g_exempt.erase(charId);
}

bool Hooks::BattleZone::IsExempt(uint32_t charId)
{
	if (charId == 0) return false;
	std::lock_guard<std::mutex> lk(g_exemptGuard);
	return g_exempt.find(charId) != g_exempt.end();
}

void __fastcall Hooks::BattleZone::CheckStepsCall(void* player)
{
	void* prev = g_currentCheckPlayer.exchange(player);
	_Player_CheckSteps(player);
	g_currentCheckPlayer.store(prev);
}

uint64_t __fastcall Hooks::BattleZone::IsActiveStartingZoneCall(void* mgr, uint64_t landHandle)
{
	const uint64_t orig = _Lands_IsActiveStartingZone(mgr, landHandle);

	// Only the low byte carries the boolean; preserve the rest of the engine's
	// return verbatim when we don't override.
	if ((orig & 0xFF) == 0) {
		return orig; // not an active starting zone — nothing to contain
	}

	const uint32_t charId = PlayerCharId(g_currentCheckPlayer.load());

	if (IsExempt(charId)) {
		// Let this player walk out: report "not active" for this check only.
		return orig & ~0xFFULL;
	}

	// Contained. Ask the script layer to show the player a notice (throttled).
	if (charId != 0 && ShouldNotify(charId)) {
		char cmd[96];
		std::snprintf(cmd, sizeof(cmd), "LifxBattleZoneOnContained(%u);", (unsigned)charId);
		Con::Evaluate(cmd);
	}
	return orig;
}
