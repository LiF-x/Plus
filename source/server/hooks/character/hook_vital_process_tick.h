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
	Hook on CharacterVitalParameters::Process_tick at RVA 0x97BC0.

	Purpose: empirical investigation. Process_tick runs every frame on every
	connected player's HP component. From the static decompile we suspect the
	live HP fields live at +0x2D8 / +0x2E0 / +0x2F8 on this object (read by:
	`(*(int64*)(this+0x2F8) - *(int64*)(this+0x2D8) + *(int64*)(this+0x2E0)) / 1e6`).

	By logging those values on every Process_tick call, we can:
	  1. Confirm the field locations
	  2. See which one moves when the player takes damage in-game
	  3. Map the field semantics by observed behavior

	The hook also captures the `this` pointer for the first connected player
	into a global, so later LiFx commands (setPlayerLiveHpDirect etc.) can
	write to those fields without needing a Player→VitalParameters lookup.

	Logging is rate-limited to once per second per player to avoid flooding
	the console — every frame would emit ~30 lines/sec/player.
*/

#include "server/cm_server.h"

#include <atomic>

__CM_DECL_EXTERNAL(void, __fastcall, _VitalParams_ProcessTick, void* self);

namespace Hooks
{
	namespace VitalParams
	{
		void ProcessTick(void* self);

		// Most-recent VitalParameters pointer seen. Useful for one-shot LiFx
		// commands that want to poke a field on the live object. Multi-player
		// hosts will need a charID-keyed table; this is a debug surface only.
		extern void* g_lastSeen;
		extern std::atomic<unsigned long long> g_callCount;

		// Manual baseline support for the wide scan. `Mark()` captures the
		// current +0x100..+0x400 window of g_lastSeen; `Diff()` compares the
		// current window against the captured baseline and echoes only the
		// fields that changed (with absolute and raw delta). Used by the
		// Lifx::vitalMark / Lifx::vitalDiff console commands.
		void Mark();
		void Diff();

		// charID → charStats registry. Populated on every Process_tick by
		// reading the charID from charStats + 0x109C (the Player+0x1B44
		// back-reference). Lookup returns nullptr if the charID hasn't
		// been seen yet.
		void* LookupCharStats(uint32_t charID);
		void  DumpRegistry();

		// Internal: called from the hook entry to register charStats. Don't
		// call from outside the hook.
		void  Internal_RegisterCharStats(void* charStats);
	}
}
