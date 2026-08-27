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
	Hook on WorkingFire::recalcTick (vtable slot 3) at RVA 0x1DB650.
	WorkingFire's slot 3 is a trivial 11-line predicate (returns whether the fire is still burning). It does NOT iterate inventory or use the descriptor table — so adding new recipes/kinds via this hook isn't possible without rewriting the function entirely. Wired primarily for completeness and to expose the predicate for fire-related instrumentation.

	Unified slot-3 signature across the craftwork class family:
	    longlong recalcTick(WorkingFire* self, float dt, char finalize);
	The engine calls all slot-3 implementations via virtual dispatch with this
	signature even when individual classes ignore the dt/finalize arguments.

	See docs/bloomery.md "Craftwork class family — full coverage matrix" for
	the full surface and which classes are useful for adding new recipes.
*/

#include "server/cm_server.h"

__CM_DECL_EXTERNAL(unsigned long long, __fastcall, _WorkingFire_RecalcTick,
                   void* self, float dt, char finalize);

namespace Hooks
{
	namespace WorkingFire
	{
		unsigned long long RecalcTick(void* self, float dt, char finalize);
	}
}
