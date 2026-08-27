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
	Hook on WorkingWindmill::recalcTick (vtable slot 3) at RVA 0x1DFF20.
	WorkingWindmill's slot 3 is a trivial 13-line timestamp update — it calls a helper and records the current time. The actual grinding logic lives elsewhere (likely under WorkingWindmill::slot 1 = 0x1DFED0). This hook is wired for completeness.

	Unified slot-3 signature across the craftwork class family:
	    longlong recalcTick(WorkingWindmill* self, float dt, char finalize);
	The engine calls all slot-3 implementations via virtual dispatch with this
	signature even when individual classes ignore the dt/finalize arguments.

	See docs/bloomery.md "Craftwork class family — full coverage matrix" for
	the full surface and which classes are useful for adding new recipes.
*/

#include "server/cm_server.h"

__CM_DECL_EXTERNAL(unsigned long long, __fastcall, _WorkingWindmill_RecalcTick,
                   void* self, float dt, char finalize);

namespace Hooks
{
	namespace WorkingWindmill
	{
		unsigned long long RecalcTick(void* self, float dt, char finalize);
	}
}
