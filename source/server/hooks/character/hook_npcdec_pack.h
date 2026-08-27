#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.

	A2a #125 — equipment-over-ghost bolt-on (server side).

	PROPER mechanism (2026-06-18): VTABLE-SLOT PATCH, not a Detours prologue
	patch. We swap the NPCDecorative vtable's packUpdate slot (slot 54, +0x1B0)
	to point at OnPackUpdate, saving the original (the NPC-only thunk 0x2E54C0,
	which jmps to the shared impl 0x0FC8B0). Only NPCDecorative instances use
	this vtable, so the gate is STRUCTURAL — real Players and items never reach
	our code. This avoids the earlier hang: Detours-prologue-patching the shared
	0x0FC8B0 rewrote a function every ShapeBase calls, and that raced world-load
	worker threads. A pointer swap rewrites nothing and has no thread race. It is
	the exact mirror of the working client side (hook_equip_unpack, slot 56).

	OnPackUpdate calls the saved original then appends the equip block; the
	client consumes exactly these bits, keeping the ghost packet byte-framed.
	A 1-bit desync corrupts every ghost after it (engine enforces pack==unpack),
	so server append and client read MUST stay lockstep.

	Signature: U64 __fastcall packUpdate(this, NetConnection* conn, U64 mask,
	                                     BitStream* stream)  -> retMask.
*  =================================================================================== */

#include "server/cm_server.h"

// BitStream::writeFlag(rcx=stream, dl=bool) -> returns the bool written.
// Resolved (not hooked) via __CM_FIND; used by the hook body to append bits.
__CM_DECL_EXTERNAL(char, __fastcall, _BitStream_WriteFlag, void* stream, char val);

// AiTree::process(rcx = tree ptr). Ticks the behaviour tree (the move-target /
// action nodes run here). Resolved via __CM_FIND; called for AI-enabled NPCs.
__CM_DECL_EXTERNAL(int, __fastcall, _AiTree_Process, void* tree);

namespace Hooks
{
	namespace NpcDecPack
	{
		using PackFn = unsigned long long(__fastcall*)(void* self, void* conn,
		                                               unsigned long long mask, void* stream);

		// Our replacement installed into NPCDecorative vtbl[54]. Runs the saved
		// original then appends the equip block for this NPC.
		unsigned long long __fastcall OnPackUpdate(void* self, void* conn,
		                                           unsigned long long mask, void* stream);

		// Swap NPCDecorative vtbl[NPCDEC_PACKUPDATE_SLOT] -> OnPackUpdate. Verifies
		// the slot currently holds the documented thunk before patching; refuses
		// (logs loud) if the layout doesn't match, so a wrong slot never calls
		// into the engine. Safe to call once the engine module is mapped.
		void InstallVtablePatch();

		// Restore the original slot pointer (best-effort).
		void RemoveVtablePatch();

		// #171 — equip-over-ghost on the native Animal vtable (bandit clothing).
		// The Animal vtable is shared by ALL wildlife, so unlike OnPackUpdate this
		// writes a per-instance marker bit (1 = our hostile, via AnimRemap::IsHostile)
		// and appends the loadout id ONLY when the marker is set — non-bandit animals
		// carry just the 0 marker, keeping every animal ghost byte-framed against the
		// client read (a 1-bit desync corrupts every later ghost).
		unsigned long long __fastcall OnAnimalPackUpdate(void* self, void* conn,
		                                                 unsigned long long mask, void* stream);

		// Swap ANIMAL_VTABLE[ANIMAL_PACKUPDATE_SLOT] -> OnAnimalPackUpdate. Verifies
		// the slot holds Animals::Animal::packUpdate (ANIMAL_PACKUPDATE_FN) first;
		// refuses (logs loud) on any layout mismatch. Loadouts are shared with the
		// NPCDecorative path via SetLoadout (keyed by object pointer).
		void InstallAnimalVtablePatch();
		void RemoveAnimalVtablePatch();

		// Enable/disable per-tick behaviour-tree ticking for an NPC. When on,
		// OnPackUpdate calls AiTree::process on the NPC's tree (creature+0x24B8)
		// each pack — making an attached tree actually run (NPCDecorative doesn't
		// tick its tree natively). Used by %npc.lifxAiTick().
		void SetAiTick(void* npc, bool on);

		// Assign a per-NPC loadout id (sent to clients in OnPackUpdate). Called
		// from the %npc.lifxLoadout(id) SimObject method — `npc` is the same
		// object pointer OnPackUpdate sees as `self`. Thread-safe vs the pack path.
		// Default (un-assigned) NPCs send loadout 0.
		void SetLoadout(void* npc, unsigned char loadoutId);
	}
}
