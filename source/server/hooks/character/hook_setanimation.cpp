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

#include "hook_setanimation.h"
#include "server/hooks/furnace/engine_internals.h"   // Engine::ModuleBase
#include "server/api/t3d_console.h"                   // Con::Echo / Con::Warning (#154 Stage 0 diag)
#include "server/api/lifx_hostile.h"                  // Hostile::RecordMountedItem (#175 mounted-loot)

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

// kernel32 GetTickCount64 (Wine-backed) — declared directly to avoid pulling windows.h.
extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);

__CM_INSTATNTIATE(_AnimatedNpc_SetAnimation);

namespace
{
	// --- our-animal registry (gates the remap to Bandit/type-755 animals) ----------
	std::atomic<bool>         g_any{false};
	std::mutex                g_mtx;
	std::unordered_set<void*> g_hostiles;

	// --- engine functions setAnimation (0x2E2A90) itself uses, resolved lazily ------
	// setAnimation body:
	//   seqMgr = *(*(self+0x920)+0x160)
	//   name   = ResolveName(nameHandle)              [0x4555A0]
	//   BuildKey(key24, name)                          [0x454FA0]
	//   h      = SeqLookupHash(seqMgr, key24)          [0x5F2880]
	//   idx    = SeqLookupIndex(seqMgr, h)             [0x5F2980]
	//   FreeKey(key24)                                 [0x86D60]
	//   if (idx != -1) SetAnimByIndex(self, idx, flag) [0x2E2520]   else log "can't find"
	using PfnResolveName   = const char* (__fastcall*)(void* handle);
	using PfnBuildKey      = void        (__fastcall*)(void* keyOut24, const char* name);
	using PfnSeqHash       = unsigned    (__fastcall*)(void* seqMgr, void* key24);
	using PfnSeqIndex      = int         (__fastcall*)(void* seqMgr, unsigned hash);
	using PfnFreeKey       = void        (__fastcall*)(void* keyOut24);
	using PfnSetByIndex    = void        (__fastcall*)(void* self, int index, char flag);
	// #154 AI-driven hit: the native melee resolve (cone hit-scan + damage).
	using PfnEndAttack     = uint64_t    (__fastcall*)(void* self);
	// #154 held-weapon render: Player::Mount_movable_object(self+0xAA8 charStats, imageTypeId).
	using PfnMountMovable  = void        (__fastcall*)(void* charStatsLike, uint32_t imageTypeId);

	PfnResolveName ResolveName = nullptr;
	PfnBuildKey    BuildKey    = nullptr;
	PfnSeqHash     SeqHash     = nullptr;
	PfnSeqIndex    SeqIndex    = nullptr;
	PfnFreeKey     FreeKey     = nullptr;
	PfnSetByIndex  SetByIndex  = nullptr;
	PfnEndAttack   EndAttack   = nullptr;
	PfnMountMovable Mount      = nullptr;
	bool           g_resolved  = false;

	// #154 held weapon: the movable-image (ShapeBaseImageData) id that renders a sword in
	// the bandit's hand (confirmed in-game via Lifx::mountBanditWeapon). 556 = the item
	// ObjectTypeID, which the resolver accepts. kCharStatsDelta = Animal->charStats subobj.
	constexpr uint32_t kBanditWeaponImageId = 556;
	constexpr unsigned kCharStatsDelta      = 0xAA8;
	std::unordered_set<void*> g_mounted;   // animals we've already mounted (guarded by g_mtx)

	// #154 field offsets on the Animal `this` (see cm_offsets.h ANIMAL_*):
	constexpr unsigned kWeaponOff     = 0x24F0;  // WeaponData* (set at spawn by onNewDataBlock)
	constexpr unsigned kAttackTypeOff = 0x24F8;  // attack-type index (0=fast,1=power)
	constexpr unsigned kConsumedOff   = 0x24FC;  // per-swing hit-consumed byte

	// #154 stamina-gated swing. The wolf AI has no stamina, so it re-issues an attack
	// every server tick (~0.5s) with nothing to pace it — unlike a player, whose swings
	// each cost stamina and who must recover when winded. We model that with a per-bandit
	// stamina pool: each swing drains kSwingCost, the pool regenerates kRegenPerSec, and
	// while it is below kSwingCost the bandit is "winded" and skips the swing (which also
	// lets the in-flight animation finish). kMinSwingMs is an animation-completion floor so
	// a full-stamina bandit still can't swing faster than the male swing can play out.
	// A simulated pool, but CALIBRATED to real LiF player values rather than arbitrary
	// numbers (the engine's real soft-stamina is a 1e6 fixed-point vital pool behind
	// virtual accessors whose regen tick doesn't run on an NPC, so draining it directly
	// would risk the bandit getting stuck winded; this reproduces the player's stamina
	// economy safely). Capacity + regen match a player's soft-stamina bar; the per-swing
	// cost is scaled by the bandit's REAL equipped weapon (the same WeaponData damage-level
	// endAttack reads), so a heavier-hitting weapon costs more stamina, clamped to a
	// player-reasonable band so an unexpected field magnitude can't produce absurd costs.
	constexpr float              kStaminaMax   = 100.0f;  // player soft-stamina bar capacity
	constexpr float              kRegenPerSec  = 9.0f;    // recovery rate (continuous; ~4s catch-breath)
	constexpr float              kBaseSwingCost = 30.0f;  // at the reference weapon; high enough that a
	                                                      //   short burst depletes the pool (~6 swings)
	constexpr float              kRefWeaponDmg = 0.30f;   // observed live Hit_group_damage_level for the
	                                                      //   bandit's weapon -> this weapon costs ~kBaseSwingCost
	constexpr float              kMinSwingCost = 10.0f;   // clamp band (keeps cost player-reasonable
	constexpr float              kMaxSwingCost = 50.0f;   //   regardless of the weapon field's scale)
	constexpr float              kRecoverFactor = 1.6f;   // once winded, recover to cost*this before resuming
	constexpr unsigned          kWeaponDmgLevelOff = 0x2390;  // WeaponData Hit_group_damage_level[0] (float)

	// #154/#168 CHARGED ATTACK — the MMO-style 3-phase melee. LiF melee animations come in
	// three aliases (see data/attack_animations.xml): <name>_prefire (charge / wind-up — the
	// weapon swings back and holds, telegraphing the strike), <name>_fire (the strike), and
	// <name>_recoil (the follow-through / recovery). We were only ever playing _fire, so there
	// was no charge tell. Now a committed attack runs prefire -> fire -> recoil on a timer
	// (driven by the per-frame OnHitTick), with the real hit fired during the FIRE phase.
	constexpr unsigned long long kPrefireMs    = 650;   // charge / wind-up: the readable telegraph
	constexpr unsigned long long kFireMs       = 450;   // the strike
	constexpr unsigned long long kRecoilMs     = 500;   // follow-through / recovery
	constexpr unsigned long long kFireContactMs = 150;  // hit fires this far into FIRE (sword extended)
	constexpr unsigned long long kAttackTotalMs = kPrefireMs + kFireMs + kRecoilMs;  // full cycle
	constexpr unsigned long long kMinSwingMs   = kAttackTotalMs;  // next attack only after recovery
	constexpr unsigned long long kSwingLockMs  = kAttackTotalMs;  // drop the tree's anim requests for the
	                                                      //   whole charge->strike->recovery so the phases
	                                                      //   play out and damage can't land swing-less

	enum AtkPhase { ATK_NONE = 0, ATK_PREFIRE, ATK_FIRE, ATK_RECOIL };

	// stamina + a "winded" hysteresis flag: when a swing can't be afforded the bandit goes
	// winded and pauses (catching its breath) until the pool recovers to cost*kRecoverFactor,
	// producing a flurry -> winded -> resume rhythm instead of a flat cadence. Plus the charged
	// attack state machine (atkPhase/atkType/phaseStartMs).
	struct BanditStam { float stamina; unsigned long long lastUpdateMs; unsigned long long lastSwingMs; bool winded;
	                    unsigned long long swingLockUntilMs;
	                    int atkPhase; int atkType; unsigned long long phaseStartMs; };
	std::unordered_map<void*, BanditStam> g_stam;   // per-bandit pool, guarded by g_mtx

	// Cached male.dts phase-sequence indices (shared shape -> stable). -2 = unresolved.
	std::atomic<int> g_seqFast[3]  {{-2}, {-2}, {-2}};   // [prefire, fire, recoil] for Attack_Fast
	std::atomic<int> g_seqPower[3] {{-2}, {-2}, {-2}};   // ... for Attack_Power
	std::atomic<int> g_seqIdle     {-2};                 // idle/rest pose played while winded
	const char* const kFastSeq[3]  = { "hit1H_leftright_prefire",  "hit1H_leftright_fire",  "hit1H_leftright_recoil"  };
	const char* const kPowerSeq[3] = { "hit1H_power_slash_prefire","hit1H_power_slash_fire","hit1H_power_slash_recoil" };

	// #154 contact-frame hit. The hit is queued (during the FIRE phase) for kFireContactMs
	// later and fired from OnHitTick, so the engine's weapon-node->hitbox trace samples the
	// sword extended mid-strike -> real body part + visual-matched timing (not the rest pose).
	std::unordered_map<void*, unsigned long long> g_pendingHit;  // animal -> fire-at ms, guarded by g_mtx

	// Per-swing stamina cost from the bandit's real equipped weapon: scale kBaseSwingCost by
	// the weapon's damage-level vs the reference, clamped to the player-reasonable band.
	float SwingCostFor(void* self)
	{
		uint64_t weapon = 0;
		std::memcpy(&weapon, static_cast<char*>(self) + kWeaponOff, sizeof(weapon));
		float cost = kBaseSwingCost;
		if (weapon) {
			float dmg = 0.0f;
			std::memcpy(&dmg, reinterpret_cast<char*>(weapon) + kWeaponDmgLevelOff, sizeof(dmg));
			if (dmg > 0.0f && dmg < 1.0e6f)   // sane field -> scale; else keep base
				cost = kBaseSwingCost * (dmg / kRefWeaponDmg);
		}
		if (cost < kMinSwingCost) cost = kMinSwingCost;
		if (cost > kMaxSwingCost) cost = kMaxSwingCost;
		return cost;
	}

	void ResolveEngineFns()
	{
		const uintptr_t b = ::Engine::ModuleBase();
		ResolveName = reinterpret_cast<PfnResolveName>(b + 0x4555A0);
		BuildKey    = reinterpret_cast<PfnBuildKey>(b + 0x454FA0);
		SeqHash     = reinterpret_cast<PfnSeqHash>(b + 0x5F2880);
		SeqIndex    = reinterpret_cast<PfnSeqIndex>(b + 0x5F2980);
		FreeKey     = reinterpret_cast<PfnFreeKey>(b + 0x86D60);
		SetByIndex  = reinterpret_cast<PfnSetByIndex>(b + 0x2E2520);
		EndAttack   = reinterpret_cast<PfnEndAttack>(b + 0x18A4D0);
		Mount       = reinterpret_cast<PfnMountMovable>(b + 0x0EBA30);
		g_resolved  = true;
	}

	// Resolve a male.dts sequence name to its index in this animal's shared seqMgr.
	int ResolveSeq(void* seqMgr, const char* name)
	{
		alignas(16) unsigned char key[32] = {0};
		BuildKey(&key, name);
		const unsigned h   = SeqHash(seqMgr, &key);
		const int      idx = SeqIndex(seqMgr, h);
		FreeKey(&key);
		return idx;
	}

	// Resolve & cache the 6 charged-attack phase indices once (male.dts is shared).
	void ResolvePhaseIndices(void* seqMgr)
	{
		if (g_seqFast[0].load(std::memory_order_relaxed) != -2) return;   // already done
		// Winded "catching breath" pose: prefer the male.dts 'fatigue' anim; fall back to idle1.
		int rest = ResolveSeq(seqMgr, "fatigue");
		if (rest < 0) rest = ResolveSeq(seqMgr, "idle1");
		g_seqIdle.store(rest, std::memory_order_relaxed);
		for (int p = 0; p < 3; ++p) {
			const int f = ResolveSeq(seqMgr, kFastSeq[p]);
			const int w = ResolveSeq(seqMgr, kPowerSeq[p]);
			g_seqFast[p].store(f, std::memory_order_relaxed);
			g_seqPower[p].store(w, std::memory_order_relaxed);
			if (f == -1 || w == -1)
				Con::Warning("[lifx-anim] #168 charged-attack seq miss: fast '%s'=%d power '%s'=%d "
				             "(phase will be skipped, timing kept)", kFastSeq[p], f, kPowerSeq[p], w);
		}
	}

	// Phase index for an attack type (0=fast,1=power) and phase (ATK_PREFIRE/FIRE/RECOIL).
	int PhaseIdx(int type, int phase)
	{
		const int p = phase - ATK_PREFIRE;   // 0..2
		if (p < 0 || p > 2) return -1;
		return (type == 1 ? g_seqPower[p] : g_seqFast[p]).load(std::memory_order_relaxed);
	}

	// #154 auto-mount the held weapon once per animal. The bandit animates from spawn
	// (idle/walk), so by the time setAnimation fires the shape instance is ready — a safe
	// mount point, the same call the manual Lifx::mountBanditWeapon command uses. Returns
	// quietly if already mounted. The mounted image replicates to clients as ShapeBase
	// image state, so every observer sees the sword.
	void MountWeaponOnce(void* self)
	{
		{
			std::lock_guard<std::mutex> lk(g_mtx);
			if (!g_mounted.insert(self).second) return;   // already mounted
		}
		void* charStatsLike = static_cast<char*>(self) + kCharStatsDelta;
		Mount(charStatsLike, kBanditWeaponImageId);
		// #175 — remember the mounted item type so it drops in the tombstone on death.
		// The mount typeId IS the item ObjectTypeID (556 = "Nordic Sword").
		Lifx::Api::Hostile::RecordMountedItem(self, kBanditWeaponImageId);
		Con::Echo("[lifx-weapon] #154 auto-mounted held weapon (image %u) on bandit %p at spawn.",
		          kBanditWeaponImageId, self);
	}

	// #154 AI-driven server-side hit: run the native Animals::Animal::endAttack cone
	// hit-scan once for this swing. The WeaponData @+0x24f0 was populated at spawn by
	// onNewDataBlock; endAttack derefs it with no null-guard, so null-check first.
	// attackType@+0x24f8 indexes the datablock attack params — keep it 0/1.
	void FireBanditHit(void* self)
	{
		uint64_t weapon = 0;
		std::memcpy(&weapon, static_cast<char*>(self) + kWeaponOff, sizeof(weapon));
		if (!weapon) return;   // no melee WeaponData -> endAttack would crash in the weapon gate
		int* atkType = reinterpret_cast<int*>(static_cast<char*>(self) + kAttackTypeOff);
		if (*atkType < 0 || *atkType > 1) *atkType = 0;
		static_cast<char*>(self)[kConsumedOff] = 0;
		EndAttack(self);
	}

	// --- wolf (AI-tree) sequence name -> male.dts sequence name ---------------------
	// male.dts has direct analogues for most; unmapped names fall back to a male idle
	// so setAnimation always resolves (no "can't find animation" flood, no broken
	// state — the animal already functions; this only fixes the visible animation).
	struct Remap { const char* wolf; const char* male; };
	const Remap kRemap[] = {
		{ "Idle_Sleep",      "Rest"                    },
		{ "Idle_Sleep_Down", "Rest_Down"               },
		{ "Idle_Sleep_Up",   "Rest_Up"                 },
		{ "Idle_Eat",        "Eat"                     },
		{ "Idle_Stand",      "idle1"                   },
		{ "Threatened",      "fright"                  },
		{ "Death",           "death"                   },
		{ "Attack_Power",    "hit1H_power_slash_fire"  },
		{ "Attack_Fast",     "hit1H_leftright_fire"    },
		{ "Run",             "Run"                     },
		{ "Walk",            "Walk_forward"            },
	};
	constexpr const char* kFallbackMaleAnim = "idle1";

	const char* MapWolfToMale(const char* wolf)
	{
		if (wolf) {
			for (const auto& r : kRemap)
				if (std::strcmp(wolf, r.wolf) == 0) return r.male;
		}
		return kFallbackMaleAnim;
	}
}

void Hooks::AnimRemap::Register(void* animal)
{
	if (!animal) return;
	std::lock_guard<std::mutex> lk(g_mtx);
	g_hostiles.insert(animal);
	g_any.store(true, std::memory_order_relaxed);
}

bool Hooks::AnimRemap::IsHostile(void* animal)
{
	if (!g_any.load(std::memory_order_relaxed)) return false;
	std::lock_guard<std::mutex> lk(g_mtx);
	return g_hostiles.find(animal) != g_hostiles.end();
}

void __fastcall Hooks::AnimRemap::OnSetAnimation(void* self, void* nameHandle, char flag)
{
	if (!IsHostile(self)) { _AnimatedNpc_SetAnimation(self, nameHandle, flag); return; }
	if (!g_resolved) ResolveEngineFns();

	// #154 ensure the bandit is holding its sword (mount once, lazily, now that the shape
	// is live). Independent of the animation being remapped below.
	MountWeaponOnce(self);

	// #154 swing protection: while a committed swing is playing, drop EVERY animation
	// request (the tree spams Face/Walk/Idle each tick, which would otherwise override the
	// swing before it plays — the cause of "damage with no swing"). The swing anim, once
	// set, plays through; the queued hit fires during this same window, so damage always
	// coincides with a visible swing.
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		auto it = g_stam.find(self);
		if (it != g_stam.end() && GetTickCount64() < it->second.swingLockUntilMs)
			return;
	}

	// Resolve the requested (wolf) name and map it to a male.dts sequence.
	const char* reqName = ResolveName ? ResolveName(nameHandle) : nullptr;
	const char* maleName = MapWolfToMale(reqName);

	// Replay setAnimation's resolve->set with the male name. seqMgr = *(*(self+0x920)+0x160).
	void* lvl1 = *reinterpret_cast<void**>(static_cast<char*>(self) + 0x920);
	if (!lvl1) { return; }
	void* seqMgr = *reinterpret_cast<void**>(static_cast<char*>(lvl1) + 0x160);
	if (!seqMgr) { return; }

	// #173 — hold the "catch-breath" rest pose for the WHOLE winded window. The rest
	// pose used to fire only on an Attack_* request while winded, so once the AI
	// switched to Walk/Face/Idle the bandit reverted to its normal pose and the
	// fatigue was never visible. Here we drop EVERY anim request while winded and
	// re-assert the idle pose (the AI spams a request each tick, so it holds; the
	// engine's same-index anti-restart guard makes the re-issue a no-op).
	ResolvePhaseIndices(seqMgr);   // ensure g_seqIdle is resolved (needs a live seqMgr)
	bool windedNow = false;
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		auto it = g_stam.find(self);
		windedNow = (it != g_stam.end() && it->second.winded);
	}
	if (windedNow) {
		const int idle = g_seqIdle.load(std::memory_order_relaxed);
		if (idle >= 0) SetByIndex(self, idle, 1);   // rest in place; engine guard de-dups
		return;                                      // drop the AI's Walk/Face/Attack request
	}

	alignas(16) unsigned char key[32] = {0};
	BuildKey(&key, maleName);
	const unsigned h   = SeqHash(seqMgr, &key);
	const int      idx = SeqIndex(seqMgr, h);
	FreeKey(&key);

	// #154 Stage 0 diagnostic: surface what the remap actually resolves so the
	// baseline test can tell "wrong male.dts name" (idx==-1 MISS -> swing silently
	// skipped) from "attack node never fired" (no log at all). Swing/attack anims and
	// every miss are always logged; other anims are throttled to confirm the hook is live.
	const bool isAttack = reqName && std::strncmp(reqName, "Attack", 6) == 0;
	if (idx == -1 || isAttack) {
		Con::Warning("[lifx-anim] #154 self=%p req='%s' -> male='%s' idx=%d%s",
		             self, reqName ? reqName : "(null)", maleName, idx,
		             idx == -1 ? "  MISS (no male.dts seq -> swing SILENTLY SKIPPED)" : "  OK");
	} else {
		static std::atomic<int> g_diagN{0};
		if (g_diagN.fetch_add(1, std::memory_order_relaxed) < 8)
			Con::Echo("[lifx-anim] #154 self=%p req='%s' -> male='%s' idx=%d OK",
			          self, reqName ? reqName : "(null)", maleName, idx);
	}

	// ------------------------------------------------------------------ #154
	// AI-driven swing + hit. The wolf AI alternates Attack_Fast (idx A) and
	// Attack_Power (idx B) every server tick; because the index keeps changing,
	// the engine's own same-index anti-restart guard (SetAnimByIndex) never trips,
	// so the male swing was restarted at frame 0 every ~0.5s and never played out.
	// We gate the swing on a per-bandit stamina pool (see g_stam): the bandit swings
	// while it has stamina, then pauses to recover when winded — dropping the surplus
	// attack re-issues so the in-flight swing also finishes. On a committed swing we run
	// the native server-side hit-scan (endAttack) once — part of the AI's natural attack.
	if (isAttack && idx != -1) {
		const unsigned long long now = GetTickCount64();
		const int atkType = (reqName && std::strcmp(reqName, "Attack_Power") == 0) ? 1 : 0;
		ResolvePhaseIndices(seqMgr);   // idempotent; needs a live seqMgr (we have one here)
		bool commit = false, restWinded = false;
		float dbgStam = 0.0f, dbgCost = 0.0f; bool dbgWinded = false;
		{
			std::lock_guard<std::mutex> lk(g_mtx);
			auto it = g_stam.find(self);
			if (it == g_stam.end())
				it = g_stam.emplace(self, BanditStam{kStaminaMax, now, 0ull, false, 0ull, ATK_NONE, 0, 0ull}).first;
			BanditStam& s = it->second;
			// NB: stamina regen + the winded-clear run continuously in OnHitTick (per frame),
			// NOT here — otherwise recovery stalls whenever the AI stops requesting attacks
			// (the cause of the ~38s freeze). Here we only read the current state and decide.

			const float cost = SwingCostFor(self);

			if (s.winded) {
				restWinded = true;   // catching breath -> play an idle so it isn't frozen
			}
			// Commit an attack only if not mid-attack, the cycle gap has passed, and there's
			// stamina. Going below one swing's worth -> winded. Starts the charged-attack state
			// machine at the PREFIRE (charge) phase; OnHitTick drives prefire->fire->recoil.
			else if (s.atkPhase == ATK_NONE &&
			         (now - s.lastSwingMs) >= kMinSwingMs && s.stamina >= cost) {
				s.stamina -= cost;
				s.lastSwingMs = now;
				commit = true;
				if (s.stamina < cost) s.winded = true;   // that was the last affordable swing
				s.swingLockUntilMs = now + kSwingLockMs; // protect the whole charge->strike->recovery
				s.atkPhase = ATK_PREFIRE; s.atkType = atkType; s.phaseStartMs = now;
			}
			dbgStam = s.stamina; dbgCost = cost; dbgWinded = s.winded;
		}
		if (restWinded) {              // winded: rest in place (visible idle) instead of freezing
			const int idle = g_seqIdle.load(std::memory_order_relaxed);
			if (idle >= 0) SetByIndex(self, idle, 1);
			return;
		}
		if (!commit) return;           // mid-attack -> drop this re-issue, let it play out
		static std::atomic<int> g_costDiagN{0};
		const int dn = g_costDiagN.fetch_add(1, std::memory_order_relaxed);
		if (dn < 5 || (dn % 4) == 0)
			Con::Echo("[lifx-stam] #154 charge: type=%s stamina=%.0f/%.0f cost=%.1f%s",
			          atkType ? "Power" : "Fast", dbgStam, kStaminaMax, dbgCost,
			          dbgWinded ? " -> WINDED (catching breath)" : "");
		const int prefireIdx = PhaseIdx(atkType, ATK_PREFIRE);   // begin the wind-up / charge
		if (prefireIdx != -1) SetByIndex(self, prefireIdx, 1);
		return;                        // OnHitTick advances PREFIRE -> FIRE (hit) -> RECOIL
	}

	if (idx != -1) SetByIndex(self, idx, flag);
	// idx == -1: silently skip (no error log). The animal still functions.
}

// Drain due contact-frame hits. Called every frame from the vital process tick (main
// thread), so endAttack runs mid-swing. Liveness: a dying bandit's pending hit is
// cancelled by Forget() from the death hook before its memory is freed; both run on the
// main thread, so there is no window where this fires on a freed animal.
void Hooks::AnimRemap::OnHitTick()
{
	if (!g_any.load(std::memory_order_relaxed)) return;
	const unsigned long long now = GetTickCount64();
	struct Play { void* self; int idx; };
	Play plays[16]; int np = 0;        // phase animations to set (engine call, do outside the lock)
	void* due[8];     int nd = 0;      // contact-frame hits to fire
	{
		std::lock_guard<std::mutex> lk(g_mtx);

		for (auto& kv : g_stam) {
			BanditStam& s = kv.second;

			// Continuous stamina regen + winded-clear — runs every frame for EVERY bandit,
			// independent of whether the AI is requesting attacks (the per-attack regen used
			// to stall when the bandit stopped attacking -> ~38s frozen "winded"). Recovery is
			// now deterministic: ~ (cost*kRecoverFactor - stamina) / kRegenPerSec seconds.
			const float dt = (now - s.lastUpdateMs) / 1000.0f;
			s.lastUpdateMs = now;
			s.stamina += kRegenPerSec * dt;
			if (s.stamina > kStaminaMax) s.stamina = kStaminaMax;
			if (s.winded && s.stamina >= SwingCostFor(kv.first) * kRecoverFactor) s.winded = false;

			// Advance the charged-attack state machine: PREFIRE -> FIRE -> RECOIL.
			if (s.atkPhase == ATK_NONE) continue;
			const unsigned long long el = now - s.phaseStartMs;
			if (s.atkPhase == ATK_PREFIRE && el >= kPrefireMs) {
				s.atkPhase = ATK_FIRE; s.phaseStartMs = now;
				if (np < 16) plays[np++] = { kv.first, PhaseIdx(s.atkType, ATK_FIRE) };
				// endAttack reads the attack-type for its damage params; set it, then schedule
				// the real hit part-way into the strike (sword extended -> correct hitbox node).
				*reinterpret_cast<int*>(static_cast<char*>(kv.first) + kAttackTypeOff) = s.atkType;
				g_pendingHit[kv.first] = now + kFireContactMs;
			} else if (s.atkPhase == ATK_FIRE && el >= kFireMs) {
				s.atkPhase = ATK_RECOIL; s.phaseStartMs = now;
				if (np < 16) plays[np++] = { kv.first, PhaseIdx(s.atkType, ATK_RECOIL) };
			} else if (s.atkPhase == ATK_RECOIL && el >= kRecoilMs) {
				s.atkPhase = ATK_NONE;   // attack complete; the swing lock expires on its own
			}
		}

		// Drain due contact-frame hits.
		for (auto it = g_pendingHit.begin(); it != g_pendingHit.end() && nd < 8; ) {
			if (now >= it->second) { due[nd++] = it->first; it = g_pendingHit.erase(it); }
			else ++it;
		}
	}
	for (int i = 0; i < np; ++i) if (plays[i].idx != -1) SetByIndex(plays[i].self, plays[i].idx, 1);
	for (int i = 0; i < nd; ++i) FireBanditHit(due[i]);   // FireBanditHit null-checks the weapon
}

void Hooks::AnimRemap::Forget(void* animal)
{
	if (!animal) return;
	std::lock_guard<std::mutex> lk(g_mtx);
	g_pendingHit.erase(animal);
	g_stam.erase(animal);
	g_mounted.erase(animal);
}
