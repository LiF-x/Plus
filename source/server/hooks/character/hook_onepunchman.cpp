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

#include "hook_onepunchman.h"
#include "hook_vital_process_tick.h"
#include "server/api/lifx_debug.h"
#include "server/hooks/furnace/engine_internals.h"

#include <mutex>
#include <unordered_set>

__CM_INSTATNTIATE(_OnePunchMan);

std::atomic<unsigned long long> Hooks::OnePunchMan::g_callCount{0};
void* Hooks::OnePunchMan::g_lastAttackerCtx = nullptr;
void* Hooks::OnePunchMan::g_lastDefenderCtx = nullptr;
void* Hooks::OnePunchMan::g_lastWeapon      = nullptr;

std::atomic<bool> Hooks::OnePunchMan::g_pendingOverride{false};
long long Hooks::OnePunchMan::g_overrideHardHp = 0;
long long Hooks::OnePunchMan::g_overrideSoftHp = 0;

// --------------------------------------------------------------------------
// Per-player PvP pacifist registry.
// --------------------------------------------------------------------------
namespace
{
	std::mutex                    g_pacifistMutex;
	std::unordered_set<uint32_t>  g_pacifist;

	// Resolve a combat ctx pointer (attacker/defender) to a live player's
	// charID. Returns 0 unless the ctx round-trips through the charID→
	// charStats registry — i.e. the candidate charID read at +0x109C maps
	// back to THIS exact ctx pointer. That double-check means we only ever
	// act on a genuine, currently-connected player's charStats; NPCs,
	// animals and any non-charStats struct yield 0 and are left untouched.
	uint32_t ResolvePlayerCharId(void* ctx)
	{
		if (!ctx) return 0;
		const uint32_t cid = *reinterpret_cast<uint32_t*>(
			static_cast<char*>(ctx) + ::Engine::kCharIdOffOnCharStats);
		if (cid == 0 || cid > 0x40000000u) return 0;
		return (Hooks::VitalParams::LookupCharStats(cid) == ctx) ? cid : 0;
	}
}

void Hooks::OnePunchMan::SetPacifist(uint32_t charID, bool on)
{
	if (charID == 0) return;
	std::lock_guard<std::mutex> lk(g_pacifistMutex);
	if (on) g_pacifist.insert(charID);
	else    g_pacifist.erase(charID);
}

bool Hooks::OnePunchMan::IsPacifist(uint32_t charID)
{
	if (charID == 0) return false;
	std::lock_guard<std::mutex> lk(g_pacifistMutex);
	return g_pacifist.count(charID) != 0;
}

void Hooks::OnePunchMan::DumpPacifist()
{
	std::lock_guard<std::mutex> lk(g_pacifistMutex);
	Con::Echo("[lifx-pacifist] %zu flagged charID(s):", g_pacifist.size());
	for (uint32_t cid : g_pacifist) Con::Echo("  charID=%u", cid);
}

void* Hooks::OnePunchMan::Call(void* attackerCtx,
                               void* outDamage,
                               char  isPrimary,
                               void* weapon,
                               void* defenderCtx,
                               int*  armorQuality,
                               char  isWarStance,
                               unsigned u8,
                               unsigned u9,
                               unsigned char u10)
{
	const auto n = ++g_callCount;
	g_lastAttackerCtx = attackerCtx;
	g_lastDefenderCtx = defenderCtx;
	g_lastWeapon      = weapon;

	if (n <= 20) {
		Con::Echo("[lifx-punch] #%llu  atk=%p  def=%p  weapon=%p  outDmg=%p  isPrimary=%d isWar=%d",
		          (unsigned long long)n,
		          attackerCtx, defenderCtx, weapon, outDamage,
		          (int)isPrimary, (int)isWarStance);
	}

	void* ret = _OnePunchMan(attackerCtx, outDamage, isPrimary, weapon,
	                         defenderCtx, armorQuality, isWarStance,
	                         u8, u9, u10);

	// Snapshot the filled output struct.
	if (outDamage) {
		auto* p = reinterpret_cast<long long*>(outDamage);
		if (n <= 20) {
			Con::Echo("[lifx-punch] #%llu  outDamage[0..3] = %lld  %lld  %lld  %lld",
			          (unsigned long long)n,
			          p[0], p[1], p[2], p[3]);
		}

		// Apply staged override if any.
		if (g_pendingOverride.exchange(false)) {
			const auto oldHard = p[0];
			const auto oldSoft = p[1];
			p[0] = g_overrideHardHp;
			p[1] = g_overrideSoftHp;
			Con::Echo("[lifx-punch] OVERRIDE applied: hardHp %lld -> %lld, softHp %lld -> %lld",
			          oldHard, p[0], oldSoft, p[1]);
		}

		// ---- Per-player PvP pacifist block --------------------------------
		// Resolve both sides to live-player charIDs (0 = not a player). If the
		// attacker is flagged pacifist AND the target is another player, wipe
		// the damage the engine just computed so the hit lands for 0 — no HP
		// loss, no wound/bleed. PvE hits (defender not a player) fall through
		// untouched, so the flagged player can still fight animals/NPCs.
		const uint32_t atkCid = ResolvePlayerCharId(attackerCtx);
		const uint32_t defCid = ResolvePlayerCharId(defenderCtx);

		if (Lifx::Debug::Enabled() && n <= 30) {
			Con::Echo("[lifx-pacifist] #%llu  atkCid=%u defCid=%u  (atk pacifist=%d)",
			          (unsigned long long)n, atkCid, defCid,
			          atkCid ? (int)IsPacifist(atkCid) : 0);
		}

		if (atkCid != 0 && defCid != 0 && IsPacifist(atkCid)) {
			const auto oldHard = p[0];
			const auto oldSoft = p[1];
			// Zero the HP-damage terms and the wound/injury aux block
			// (+0x18..+0x38, i.e. p[3..6]) so neither HP nor wounds apply.
			p[0] = 0;  // hard HP damage
			p[1] = 0;  // soft HP damage
			p[3] = 0;
			p[4] = 0;
			p[5] = 0;
			p[6] = 0;
			Con::Echo("[lifx-pacifist] PvP hit nullified: attacker charID=%u -> player charID=%u "
			          "(hardHp %lld, softHp %lld dropped to 0)",
			          atkCid, defCid, oldHard, oldSoft);
		}
	}

	return ret;
}
