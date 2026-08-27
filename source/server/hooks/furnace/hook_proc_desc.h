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
	Hook on WorkingFurnace's process-descriptor lookup (RVA 0x1DB7C0).

	The engine calls this function once per item per furnace tick to find the
	row in DAT_140ACFA60 that determines how the item is processed (which
	branch of WorkingFurnace::recalcTick runs for it). The default behavior:
	  - walks the in-binary table at DAT_140ACFA60
	  - for each row, tests hasParent(item_type, row.typeId, 100)
	  - returns the first matching row (28-byte struct), or NULL if none

	We install a Detours hook so LiFx can interpose its own extension table
	BEFORE the original lookup runs. The intent is to let server operators add
	new furnace recipes (clay-baking, vostaskus-style cycles, etc.) without
	patching the binary.

	This first iteration is a pure passthrough: we install the hook and call
	the original immediately. That proves the install/detour works end-to-end
	with zero behavioral change. The extension-table consultation will be
	added in a follow-up commit.

	See docs/bloomery.md for the full furnace architecture and the layout of
	the process-descriptor table.
*/

#include "server/cm_server.h"

// The original FUN_1401DB7C0 takes a single pointer (the item's type-info
// pointer) and returns a pointer to a 28-byte row in the descriptor table.
// MSVC x64 has one calling convention; we use __fastcall to match the existing
// hook style in hooks_engine.cpp.
__CM_DECL_EXTERNAL(void*, __fastcall, _Furnace_LookupProcDesc, void* itemTypeInfo);

namespace Hooks
{
	namespace Furnace
	{
		// Detour target. Currently passthrough; future iterations will consult
		// an LiFx-loaded extension table before falling through to the engine
		// lookup.
		void* ProcDescLookup(void* itemTypeInfo);
	}
}
