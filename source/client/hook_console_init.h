#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	Hook on the client engine's Con::init(). Fires after the engine
	finishes its own console init (we call the original first), at
	which point Con::InternalConsolePrintf is safe to call. This is
	the seam where any "register a client TS command" or similar
	engine-touching work belongs — never DllMain.
*  =================================================================================== */

#include "client/client_aux.h"

__LIFX_DECL_EXTERNAL(void, __fastcall, _Engine_Con_Init, void);

// Con::evaluate(const char* str, bool echo, const char* fileName) — RE'd in
// chunk 15c part 2. Used from OnConsoleInit to register TS-level glue
// (clientCmdGoToServer) at runtime, avoiding a full Con::addCommand RE
// for each of the 5 return-type variants. The script-defined function is
// invoked exactly like a C++ addCommand entry when commandToClient
// forwards a "GoToServer" verb from the server.
__LIFX_DECL_EXTERNAL(const char*, __fastcall, _Engine_Con_Evaluate,
                     const char* string, bool echo, const char* fileName);

namespace LifxClient
{
	namespace HookConsoleInit
	{
		void OnConsoleInit();
	}
}
