#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	A2a equip-render (issue #125). Detours the shipped console command
	tmpHideAllNakedMans (CLIENT_TMP_HIDE_NAKED): the stock command queries
	renderable objects (mask 0x8000) and hides a fixed 8-mesh body set; we
	replace that with the 361 baked armor/clothing meshes from male.dts so a
	player-model NPCDecorative renders as a naked body instead of every armor
	variant overlapping. Triggered from script via tmpHideAllNakedMans() (the
	server fires it with commandToClient -> clientCmdLifxNaked).
*  =================================================================================== */

#include "client/client_aux.h"

// Engine fns called from the detour (resolved in AttachHooks).
__LIFX_DECL_EXTERNAL(void, __fastcall, _Engine_QueryRenderObjects,
                     void* container, unsigned mask, void* outVec);
__LIFX_DECL_EXTERNAL(void, __fastcall, _Engine_SetMeshHidden,
                     void* obj, const char* meshName, char hidden);

// The detoured console command (same signature as a Torque VoidCallback).
__LIFX_DECL_EXTERNAL(void, __fastcall, _Engine_TmpHideAllNaked,
                     void* obj, int argc, const char** argv);

namespace LifxClient
{
	namespace HookNakedRender
	{
		void __fastcall OnHideAllNaked(void* obj, int argc, const char** argv);
	}
}
