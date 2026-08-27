#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	A2a #125 — equipment-over-ghost bolt-on (client/receive side).

	The NPC ghost unpacks through the NetObject ghost dispatch's virtual call
	`call [vtbl + 115*8]` (unpack slot). Detouring the shared function
	(Player::unpackUpdate 0x20CA90) did NOT catch the NPC — so we patch the
	NPCDecorative vtable slot 115 (CLIENT_NPCDEC_VTABLE + 0x398) directly. That
	intercepts the dispatch's call for NPCDecorative ghosts specifically,
	independent of which function the slot points to. The patch runs the original
	first (consuming the standard ghost fields) then reads the bits the server
	appended in packUpdate.

	Increment 1b (diag): server append is OFF and we only log the call + vtable so
	we can confirm the patch fires for the NPC without desyncing the packet.
*  =================================================================================== */

#include "client/client_aux.h"

namespace LifxClient
{
	namespace HookEquipUnpack
	{
		// Patch NPCDecorative vtbl[115] -> OnUnpackUpdate. Call from AttachHooks.
		void InstallVtablePatch();
		void RemoveVtablePatch();

		void __fastcall OnUnpackUpdate(void* self, void* conn, void* stream);

		// #171 — receive side for native Animal bandits. Patches the Animal vtable's
		// unpack slot (CLIENT_ANIMAL_VTABLE, slot 56). The Animal vtable is shared by
		// all wildlife, so this ALWAYS reads the 1-bit marker the server wrote and
		// only reads+applies the 8 loadout bits when it is set (marker 0 = real
		// animal -> nothing more to read). Keeps every animal ghost byte-framed.
		void InstallAnimalVtablePatch();
		void RemoveAnimalVtablePatch();

		void __fastcall OnAnimalUnpackUpdate(void* self, void* conn, void* stream);
	}
}
