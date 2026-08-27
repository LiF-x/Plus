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
	Hook on BrewingTankFurnace's process-descriptor lookup at RVA 0x1DAAE0.

	Brewing has its OWN descriptor table at DAT_140ACF8D0, distinct from the
	furnace table at DAT_140ACFA60. Both tables follow the same 28-byte row
	layout (typeId, kind, factor, outputTypeId, flag, field5, tempThreshold)
	and both lookups follow the same hasParent-based matching algorithm.

	The shared furnace proc-desc hook (hook_proc_desc.cpp) only intercepts
	WorkingFurnace's calls. To extend brewing recipes you need this separate
	hook because BrewingTankFurnace::recalcTick (at 0x1DB370) calls into
	FUN_1401DAAE0, not FUN_1401DB7C0.

	The brewing-side hasParent filter is 0x457 (type 1111) — that's the
	"brewable" parent type, the brewing analog of 0xCE/0xD5 for bloomery.

	To dump the brewing descriptor table:
	    scripts/dump_furnace_table.py --rva 0xACF8D0
*/

#include "server/cm_server.h"

__CM_DECL_EXTERNAL(void*, __fastcall, _BrewingTankFurnace_LookupProcDesc,
                   void* itemTypeInfo);

namespace Hooks
{
	namespace BrewingTankFurnace
	{
		void* ProcDescLookup(void* itemTypeInfo);
	}
}
