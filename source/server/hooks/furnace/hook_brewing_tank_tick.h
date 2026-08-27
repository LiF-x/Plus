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
	Hook on BrewingTankFurnace::recalcTick (RVA 0x1DB370).

	BrewingTankFurnace is the sibling of WorkingFurnace under
	AbstractCraftworkFurnace. Its slot-3 implementation (the per-tick driver)
	is different from WorkingFurnace's, so it needs its own hook — patching
	WorkingFurnace::recalcTick alone wouldn't reach brewing-tank instances.

	Both classes share AbstractCraftworkFurnace::Craft (slot 6) and other
	base-class methods (slots 1, 2, 4, 6, 7, 10), so the descriptor lookup
	hook (hook_proc_desc.cpp) already applies to brewing-tank items. This
	hook is only required if you want to define BRAND NEW `kind` values that
	BrewingTankFurnace's hardcoded switch wouldn't otherwise recognize.

	Function signature is identical to WorkingFurnace::recalcTick:
	    longlong recalcTick(BrewingTankFurnace* self, float dt, char finalize);

	See hook_working_furnace_tick.h for the per-tick anatomy and the design
	notes that apply equally here.
*/

#include "server/cm_server.h"

__CM_DECL_EXTERNAL(unsigned long long, __fastcall, _BrewingTankFurnace_RecalcTick,
                   void* self, float dt, char finalize);

namespace Hooks
{
	namespace BrewingTankFurnace
	{
		unsigned long long RecalcTick(void* self, float dt, char finalize);
	}
}
