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

#include "hook_brewing_tank_tick.h"
#include "engine_internals.h"

__CM_INSTATNTIATE(_BrewingTankFurnace_RecalcTick);

// ============================================================================
// ENGINE REFERENCE — BrewingTankFurnace::recalcTick @ RVA 0x1DB370
// Verbatim Ghidra decompile lives at /tmp/lifx_ghidra/decompile/rt_1DB370.c
// (112 lines). Distilled control flow:
//
//   if (furnaceTemperature(self) != 0) {
//       beginInventoryIteration(iter, contents);
//       if (iterationValid(iter)) {
//           for each slot in inventory {
//               if (slot.itemData != NULL &&
//                   hasParent(slot.itemType, 0x457=1111 "brewable", 100)) {
//                   row = brewingDescLookup(slot.itemType);
//                   if (row == NULL) {
//                       altRow = brewingDescLookupAlt(slot.itemType);
//                       if (altRow == NULL) abort(iter, slot);
//                   }
//                   else if (row.kind == 2) {
//                       if (!finalize) {
//                           temp = furnaceTemperature(self);
//                           if (temp < row.tempLow || temp > row.tempHigh) {
//                               // out-of-range: regress quality
//                               if (slot.quality > 10)
//                                   inv_advanceAlt(slot, (quality / 20) * dt + 0.5);
//                           }
//                           else if (slot.quality < 100) {
//                               inc = (100 / row.duration) * dt + 0.5;
//                               if (slot.quality + inc < 100)
//                                   inv_advance(slot, inc);
//                               else
//                                   inv_setQuality(slot, 100);
//                           }
//                       }
//                       else {
//                           inv_setQuality(slot, 100);   // finalize forces done
//                       }
//                   }
//               }
//           }
//           finishIteration(iter);
//           tickRate = self->vtable[8](self);            // get tick-rate hint
//           self->vtable[7](self);                       // consumeWoodForFuel
//           scaledRate = (uint)((float)tickRate * dt);
//           if (currentTemp < scaledRate) {              // overheated
//               temp = furnaceTemperature(self);
//               if (temp > 10) furnaceSetTemperature(self, temp - 1);
//           }
//           else {
//               currentTemp -= scaledRate;               // cool down by scaledRate
//           }
//       }
//       teardownIteration(iter);
//   }
//   return 0;
// ============================================================================

namespace LifxImpl
{
	namespace BrewingTankFurnace
	{
		// Brewing-specific filter: the engine only processes inventory slots
		// whose item type descends from type 1111 ("brewable").
		constexpr int kBrewableAncestorTypeId = 0x457;

		// Helper: extract a slot's item type-info pointer (or nullptr if empty).
		static void* SlotItemTypeInfo(void* slot)
		{
			auto itemData = *reinterpret_cast<void**>(
				static_cast<char*>(slot) + ::Engine::Off::Slot_ItemData);
			return itemData;
		}

		unsigned long long RecalcTick(void* self, float dt, char finalize)
		{
			// 1. Bail if the tank isn't lit / running.
			if (::Engine::Furnace_GetTemperature(self) == 0)
				return 0;

			// 2. Construct the inventory iterator over self->contents (at +0x0C).
			alignas(8) char iter[72] = {};
			::Engine::Inv_Begin(iter, static_cast<char*>(self) + ::Engine::Off::Furnace_ContentsRef);

			if (::Engine::Inv_Valid(iter))
			{
				// 3. Engine walks the inventory via its own internal traversal —
				//    we can't replicate the bucket-walk over self->contents
				//    structure from this level without much more decompile work.
				//    Delegate the per-slot processing to the engine's tick;
				//    teardown still runs from here so the iterator is balanced.
				//
				//    Replacing this delegate with a faithful per-slot loop is a
				//    future LiFx exercise — the per-slot logic (kind=2 branch,
				//    temperature range, quality progression) is fully visible in
				//    the engine reference above and uses helpers that are
				//    already wired in engine_internals.h.
				//
				//    Note: calling the trampoline here would re-enter this hook.
				//    Use the engine's original directly via ::Engine::At...:
				typedef unsigned long long (__fastcall *pfn)(void*, float, char);
				static const auto engineTick = ::Engine::AtRva<pfn>(0x1DB370);
				engineTick(self, dt, finalize);
			}

			::Engine::Inv_Teardown(iter);
			return 0;
		}
	}
}

namespace { constexpr bool kUseLifxReimplementation = false; }

unsigned long long Hooks::BrewingTankFurnace::RecalcTick(void* self, float dt, char finalize)
{
	if constexpr (kUseLifxReimplementation)
		return LifxImpl::BrewingTankFurnace::RecalcTick(self, dt, finalize);
	return _BrewingTankFurnace_RecalcTick(self, dt, finalize);
}
