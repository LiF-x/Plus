/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_apply_damage.h"
#include "server/api/lifx_debug.h"

__CM_INSTATNTIATE(_Hit_ApplyDamage);

std::atomic<unsigned long long> Hooks::HitApplyDamage::g_callCount{0};
void* Hooks::HitApplyDamage::g_lastCharStats = nullptr;
void* Hooks::HitApplyDamage::g_lastDmgPacket = nullptr;

std::atomic<bool> Hooks::HitApplyDamage::g_pendingOverride{false};
long long Hooks::HitApplyDamage::g_overrideHardHp = 0;
long long Hooks::HitApplyDamage::g_overrideSoftHp = 0;

void Hooks::HitApplyDamage::Call(void* charStats, void* dmgPacket)
{
	const auto n = ++g_callCount;
	g_lastCharStats = charStats;
	g_lastDmgPacket = dmgPacket;

	// Read first 4 int64 of dmgPacket — should include hard/soft HP damage.
	long long pre0 = 0, pre1 = 0, pre2 = 0, pre3 = 0;
	if (dmgPacket) {
		auto* p = reinterpret_cast<long long*>(dmgPacket);
		pre0 = p[0]; pre1 = p[1]; pre2 = p[2]; pre3 = p[3];
	}

	// Optional staged override — write our values into the packet before
	// the engine sees it. Sign convention: positive = damage to apply.
	bool didOverride = false;
	long long ovHard = 0, ovSoft = 0;
	if (dmgPacket && g_pendingOverride.exchange(false)) {
		ovHard = g_overrideHardHp;
		ovSoft = g_overrideSoftHp;
		auto* p = reinterpret_cast<long long*>(dmgPacket);
		p[0] = ovHard;
		p[1] = ovSoft;
		didOverride = true;
	}

	// Overrides always log (rare + significant). The first-30 trace is
	// debug-gated.
	if (didOverride || (Lifx::Debug::Enabled() && n <= 30)) {
		Con::Echo("[lifx-apply] #%llu  charStats=%p  pkt=%p  pkt[0..3]=%lld %lld %lld %lld%s",
		          (unsigned long long)n, charStats, dmgPacket,
		          pre0, pre1, pre2, pre3,
		          didOverride ? "  OVERRIDDEN" : "");
		if (didOverride) {
			Con::Echo("[lifx-apply]   override: hardHp=%lld softHp=%lld", ovHard, ovSoft);
		}
	}

	_Hit_ApplyDamage(charStats, dmgPacket);
}
