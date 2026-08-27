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
	Hook on CharacterParameters::Calc_hit_damage_distribution at RVA 0x91A50.

	Empirical investigation hook. The engine calls this function every time
	a real combat hit is processed against a character. By logging the
	entry args + taking before/after snapshots of fields likely to contain
	HP state, we can identify the actual HP-write location at runtime —
	bypassing the static-analysis dead-end that Process_tick led us into.

	Signature from the decompile:
	    void* fn(void* self, void** out, int* result, void* attacker, void* extra);

	The "self" is presumably the CharacterParameters of the defender. We
	walk a small contiguous slice of its memory (offsets +0x2D0..+0x300)
	before and after the call, log any int64 fields that changed, and also
	capture the self pointer so a separate Lifx command can poke fields.

	Rate-limit: log first N invocations unconditionally then settle into
	"log on change only" so the console doesn't flood under heavy combat.

	See docs/character_hp.md "Recommended next move (Option B)" for context.
*/

#include "server/cm_server.h"

#include <atomic>

__CM_DECL_EXTERNAL(void*, __fastcall, _Char_CalcHitDamage,
                   void* self, void* out, int* result, void* attacker, void* extra);

namespace Hooks
{
	namespace CharCalcHitDamage
	{
		void* Call(void* self, void* out, int* result, void* attacker, void* extra);

		extern std::atomic<unsigned long long> g_callCount;
		extern void* g_lastSelf;
	}
}
