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
	Hook on the "ONEPUNCHMAN" damage calculator at RVA 0x0A4BF0.

	Signature (from decompile dd_A4BF0.c):

	    longlong* FUN_1400a4bf0(
	        longlong *attackerCtx,    // p1 character params struct of attacker
	        longlong *outDamage,      // p2 OUTPUT — 64-byte struct filled with:
	                                  //     +0x00 hardHpDamage (int64, ×1e6 scale)
	                                  //     +0x08 softHpDamage (int64, ×1e6 scale)
	                                  //     +0x10 bodyPart info (uint16)
	                                  //     +0x12 flag (uint8)
	                                  //     +0x18..+0x38 wound/injury aux state
	        char     isPrimary,
	        longlong weapon,          // p4 weapon/hit descriptor
	        longlong defenderCtx,     // p5 defender character params
	        int     *armorQuality,
	        char     isWarStance,
	        unsigned u8, unsigned u9, unsigned char u10);

	This is the real combat path (the entry point emits the ONEPUNCHMAN log
	lines we see). After we return, the engine reads `outDamage` and applies
	it through the (LTO-inlined) wound-update code.

	Telemetry plan:
	  - Log first 20 entries with attacker/defender/weapon pointers.
	  - Read back the first 8 int64 fields of `outDamage` after the engine
	    fills it, so we can see the actual damage values the engine computes.

	Mutation plan (next step, once telemetry confirms hook fires):
	  - Expose Lifx::nextHitForce(hardHp, softHp) to stage an override.
	  - On return, overwrite outDamage[0]/[1] with the staged values so the
	    very next combat hit applies our chosen damage instead of the
	    engine-computed amount.
*/

#include "server/cm_server.h"

#include <atomic>
#include <cstdint>

__CM_DECL_EXTERNAL(void*, __fastcall, _OnePunchMan,
                   void* attackerCtx,
                   void* outDamage,
                   char  isPrimary,
                   void* weapon,
                   void* defenderCtx,
                   int*  armorQuality,
                   char  isWarStance,
                   unsigned u8,
                   unsigned u9,
                   unsigned char u10);

namespace Hooks
{
	namespace OnePunchMan
	{
		void* Call(void* attackerCtx,
		           void* outDamage,
		           char  isPrimary,
		           void* weapon,
		           void* defenderCtx,
		           int*  armorQuality,
		           char  isWarStance,
		           unsigned u8,
		           unsigned u9,
		           unsigned char u10);

		extern std::atomic<unsigned long long> g_callCount;
		extern void* g_lastAttackerCtx;
		extern void* g_lastDefenderCtx;
		extern void* g_lastWeapon;

		// Staged override for the NEXT hit. If g_pendingOverride is true,
		// the hook will overwrite outDamage[+0x00] and outDamage[+0x08] with
		// these values (in ×1e6 scale) then clear the flag.
		extern std::atomic<bool> g_pendingOverride;
		extern long long g_overrideHardHp;
		extern long long g_overrideSoftHp;

		// ---- Per-player PvP pacifist toggle -------------------------------
		// When a player's charID is flagged pacifist, every melee hit they
		// land on ANOTHER PLAYER has its hard/soft HP (and wound-aux) damage
		// zeroed before the engine applies it — i.e. their weapons "can not
		// hurt other players", the generalised believer-weapon behaviour but
		// per-player and for ANY weapon. PvE (animals/NPCs) and world damage
		// are unaffected: only attacker→player hits are zeroed.
		//
		// Attacker/defender identity is resolved from the ctx pointers and
		// validated by round-tripping through the charID→charStats registry
		// (Hooks::VitalParams), so a ctx that isn't a live player's charStats
		// simply leaves damage untouched (fail-safe — combat never breaks).
		//
		// Thread-safe: the combat path reads via IsPacifist(); console
		// commands mutate via SetPacifist(). Guarded by an internal mutex.
		void SetPacifist(uint32_t charID, bool on);
		bool IsPacifist(uint32_t charID);
		void DumpPacifist();
	}
}
