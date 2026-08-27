/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).
*  =================================================================================== */

#include "hook_console.h"

#include <cstdio>

__LIFX_INSTANTIATE(_Engine_Con_InternalConsolePrintf);

void LifxClient::HookConsole::OnInternalPrintf(std::uint32_t type, std::uint32_t unk1, char* buffer)
{
	char prefixed[8200];
	std::snprintf(prefixed, sizeof(prefixed), "[LiFx-client] %s", buffer);
	_Engine_Con_InternalConsolePrintf(type, unk1, prefixed);
}
