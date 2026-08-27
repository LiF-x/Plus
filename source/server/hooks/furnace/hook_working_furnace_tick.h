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
	Hook on WorkingFurnace::recalcTick (RVA 0x1DCFF0).

	This is the per-frame heartbeat for every WorkingFurnace instance (bloomery,
	kiln, smelter — NOT brewing tanks; those subclass BrewingTankFurnace and
	override the vtable slot with their own implementation at 0x1DB370).

	The engine's recalcTick has a hardcoded switch over the `kind` value read
	from each item's process-descriptor row. Values 1-8 are handled in-engine;
	anything else falls through silently. To implement entirely new processing
	cycles (e.g., a 3-stage cooling cycle, a temperature-gradient curve, an
	hours-of-day-of-week filter — anything you can express in C++), we hook
	recalcTick itself: inspect each item's descriptor, branch on our extension
	kinds first, then delegate the rest to the engine.

	Function signature (matched against the decompile):

	    longlong recalcTick(WorkingFurnace* self, float dt, char finalize);

	On MSVC x64 the `float dt` argument is passed in XMM1, not RDX, because of
	the special handling for floating-point args in __fastcall. The compiler
	does this automatically when we declare the prototype faithfully.

	Currently this hook is a pure passthrough — it calls the original and
	returns its result. The dispatch-on-extension-kind logic is laid out in
	the .cpp as commented scaffolding.

	See docs/bloomery.md "recalcTick — full walkthrough" for the engine's own
	branch-by-branch behavior.
*/

#include "server/cm_server.h"

__CM_DECL_EXTERNAL(unsigned long long, __fastcall, _WorkingFurnace_RecalcTick,
                   void* self, float dt, char finalize);

namespace Hooks
{
	namespace WorkingFurnace
	{
		unsigned long long RecalcTick(void* self, float dt, char finalize);
	}
}
