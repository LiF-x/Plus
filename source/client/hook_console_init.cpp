/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).
*  =================================================================================== */

#include "hook_console_init.h"
#include "hook_console.h"
#include "hook_equip_unpack.h"

#include <cstdio>

__LIFX_INSTANTIATE(_Engine_Con_Init);

void LifxClient::HookConsoleInit::OnConsoleInit()
{
	// Engine console must finish initializing before we can safely
	// route anything through Con::InternalConsolePrintf. Run the
	// original first.
	_Engine_Con_Init();

	// First proof-of-life log line via the engine console — exercises
	// both the printf hook (prefixes "[LiFx-client] ") and confirms
	// our Con::Init hook actually fired during client startup.
	char msg[128];
	std::snprintf(msg, sizeof(msg),
	              "engine console initialized; LiFx client hooks live (pid=%lu)\n",
	              static_cast<unsigned long>(GetCurrentProcessId()));
	_Engine_Con_InternalConsolePrintf(0, 0, msg);

	// Register the federation handoff entry point as a TS function. The
	// server uses commandToClient(client, 'GoToServer', charID, peerWorldId,
	// "host:port"), which Torque dispatches to the global clientCmd<verb>.
	// Body just echoes for now — the actual NetConnection swap lands in
	// the next chunk (#106 chunk 16/17). Defined via Con::evaluate instead
	// of Con::addCommand so we don't have to RE all 5 AddCommand variants
	// just to ship the dispatch seam.
	_Engine_Con_Evaluate(
		"function clientCmdGoToServer(%charID, %peerWorldId, %addr) {"
		"    echo(\"clientCmdGoToServer charID=\" @ %charID"
		"          @ \" peerWorldId=\" @ %peerWorldId"
		"          @ \" addr=\" @ %addr);"
		"}",
		false, "lifx_client_glue");

	// A2a (#125): server fires commandToClient(%client, 'LifxNaked') to cull the
	// armor meshes on player-model NPCs. tmpHideAllNakedMans is detoured (see
	// hook_naked_render) to hide the 361 armor meshes instead of the body set.
	_Engine_Con_Evaluate(
		"function clientCmdLifxNaked() { tmpHideAllNakedMans(); }",
		false, "lifx_client_glue");

	// A2a (#125): equipment now renders PER-INSTANCE off the ghost stream — the
	// server vtable-patches NPCDecorative::packUpdate to append an equip block,
	// and the client unpack patch (below) reads it and applies meshes the moment
	// the NPC scopes in. That is the proper, scope-correct path, so the old
	// self-rescheduling sweep tick is retired. (clientCmdLifxNaked stays as a
	// manual GM fallback; it just isn't auto-driven anymore.)

	// A2a (#125) equip-over-ghost RECEIVE side: patch NPCDecorative unpack
	// vtbl[56] now that the engine console is up (safe to diag() here). This
	// reads the equip block the server appends in its OWN vtable-patched
	// packUpdate (slot 54) and applies the meshes per NPC instance. Both sides
	// are now vtable patches — NOT Detours prologue patches on the shared
	// pack/unpack impls — so neither races world-load. Bit format is lockstep:
	// marker(1) + count(8) + count*index(16).
	LifxClient::HookEquipUnpack::InstallVtablePatch();
	// #171 — same receive side on the native Animal vtable (slot 56) so our bandits
	// render armor. Reads the per-instance marker the server appends in the Animal's
	// packUpdate (slot 54): marker(1) + loadoutId(8) only when the marker is set.
	LifxClient::HookEquipUnpack::InstallAnimalVtablePatch();
}
