#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	Hook on the client engine's Con::InternalConsolePrintf — same
	signature as the server's (U32 type, U32 unk1, char* buffer).
	Prefixes every console line with "[LiFx-client] " so we can tell
	mod output apart from stock engine output in the client log.
*  =================================================================================== */

#include "client/client_aux.h"
#include <cstdint>

__LIFX_DECL_EXTERNAL(void, __fastcall, _Engine_Con_InternalConsolePrintf,
                     std::uint32_t type, std::uint32_t unk1, char* buffer);

namespace LifxClient
{
	namespace HookConsole
	{
		void OnInternalPrintf(std::uint32_t type, std::uint32_t unk1, char* buffer);
	}
}
