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

#include "hook_brewing_tank_desc.h"
#include "engine_internals.h"

__CM_INSTATNTIATE(_BrewingTankFurnace_LookupProcDesc);

// ============================================================================
// ENGINE REFERENCE — BrewingTankFurnace's process-descriptor lookup @ RVA 0x1DAAE0
// Walks DAT_140ACF8D0 (the brewing descriptor table). Same algorithm as the
// furnace lookup, distinct table.
// ----------------------------------------------------------------------------
// undefined4 * FUN_1401daae0(undefined8 param_1)
// {
//   char cVar1;
//   longlong lVar2;
//   int iVar3;
//   longlong lVar4;
//
//   iVar3 = 0;
//   if (DAT_140acf8d4 != 0) {                            // table not empty
//     lVar4 = 0;
//     lVar2 = 0;
//     do {
//       cVar1 = FUN_14027eb30(param_1,
//                             *(undefined4 *)((longlong)&DAT_140acf8d0 + lVar2),
//                             100);                     // hasParent(item, row.typeId, 100)
//       if (cVar1 != '\0') {
//         return &DAT_140acf8d0 + (longlong)iVar3 * 7;  // return matching row
//       }
//       lVar4 = lVar4 + 1;
//       iVar3 = iVar3 + 1;
//       lVar2 = lVar4 * 0x1c;                            // advance 28 bytes (7 dwords)
//     } while ((&DAT_140acf8d4)[lVar4 * 7] != 0);        // terminator = first int == 0
//   }
//   return (undefined4 *)0x0;
// }
// ============================================================================

namespace LifxImpl
{
	namespace BrewingTankFurnace
	{
		// Brewing descriptor table — same 28-byte row layout as DAT_140ACFA60,
		// but indexed by the brewing system's own typeIds.
		struct DescRow {
			uint32_t typeId;          // +0x00
			uint32_t kind;            // +0x04
			uint32_t factor;          // +0x08
			uint32_t outputTypeId;    // +0x0C
			uint32_t flag;            // +0x10
			uint32_t field5;          // +0x14
			uint32_t tempThreshold;   // +0x18
		};
		static_assert(sizeof(DescRow) == 28, "must match engine layout");

		static DescRow* GetTable() {
			// DAT_140ACF8D0 — the brewing descriptor table.
			return ::Engine::DataAtRva<DescRow>(0xACF8D0);
		}

		void* ProcDescLookup(void* itemTypeInfo) {
			auto* rows = GetTable();
			// Walk until a row whose typeId is 0 (the engine's sentinel).
			for (unsigned i = 0; rows[i].typeId != 0; ++i) {
				if (::Engine::Type_HasParent(itemTypeInfo, static_cast<int>(rows[i].typeId), 100))
					return &rows[i];
			}
			return nullptr;
		}
	}
}

namespace { constexpr bool kUseLifxReimplementation = false; }

void* Hooks::BrewingTankFurnace::ProcDescLookup(void* itemTypeInfo)
{
	if constexpr (kUseLifxReimplementation)
		return LifxImpl::BrewingTankFurnace::ProcDescLookup(itemTypeInfo);
	// Default: pass straight through to the engine. To extend brewing recipes,
	// add an LifxFurnaceRow extension table walk above this line (see
	// hook_proc_desc.cpp for the worked pattern), then fall through.
	return _BrewingTankFurnace_LookupProcDesc(itemTypeInfo);
}
