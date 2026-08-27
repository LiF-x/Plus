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
	Hook on CmCharacterWounds::dealDamage at RVA 0x1C63E0.

	Signature (from decompile dd_1C63E0.c):
	    void __fastcall dealDamage(CmCharacterWounds* this, int bodyPart);

	`bodyPart` is 0..5. The engine logs "CmCharacterWounds::dealDamage(<name>)"
	on entry for any value < 6.

	Purpose: capture a live `CmCharacterWounds*` for the most recent player
	that took damage. With that pointer in hand, the Lifx::dealDamage command
	can re-invoke the engine's own dealDamage on demand, producing a real
	in-engine damage event (which broadcasts to the client and moves the HUD).
*/

#include "server/cm_server.h"

#include <atomic>

__CM_DECL_EXTERNAL(void, __fastcall, _Wounds_DealDamage,
                   void* self, int bodyPart);

namespace Hooks
{
	namespace WoundsDealDamage
	{
		void Call(void* self, int bodyPart);

		extern std::atomic<unsigned long long> g_callCount;
		extern void* g_lastSelf;
		extern int   g_lastBodyPart;
	}
}
