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

#include "hook_working_furnace_tick.h"
#include "hook_proc_desc.h"
#include "engine_internals.h"

__CM_INSTATNTIATE(_WorkingFurnace_RecalcTick);

// ============================================================================
// ENGINE REFERENCE — WorkingFurnace::recalcTick @ RVA 0x1DCFF0
//
// 352 lines of decompile at /tmp/lifx_ghidra/decompile/rt_1DCFF0.c — too long
// to inline here. Full English walkthrough lives in docs/bloomery.md
// §"recalcTick — full walkthrough". The high-level structure:
//
//   if (self->state == 0) return 0;             // furnace off, bail
//   dt = clamp(dt, 0, 1.0);
//   getContents(self, &contents);
//   iVar7 = TypeOf(contents)->kindTag;          // furnace kind: bloomery, kiln…
//
//   for each item in furnace.inventory {
//       row = procDescLookup(item.type);
//       if (row == NULL || row.flag != 0) {
//           // Path A: recipe-driven (bloomery's metallic filter)
//       } else if (iVar7 == 0x75) {
//           // Path B: furnace-kind-117 dispatch on row.kind
//       } else {
//           // Path C: generic dispatch on row.kind
//           //   kind=4,6   single-stage bake
//           //   kind=5     vostaskus heat/cool/heat cycle
//           //   kind=2,3   instant complete
//       }
//   }
//
//   // Tick-rate post-processing:
//   //   - call self->vtable[8](self) to get burn rate
//   //   - subtract from self->temperature
//   //   - if overheated, decrement temperature based on iVar7 group
// ============================================================================

// ----------------------------------------------------------------------------
// Defining a brand-new "kind" — extension cookbook (unchanged from previous
// iteration; see comments inline for the "Strategy A" plan).
//
// To wire up a custom cycle (e.g., kind=100):
//   1. Add a row to kExtensionRows[] in hook_proc_desc.cpp with kind=100.
//   2. Implement that kind's behavior in LifxImpl::WorkingFurnace::RecalcTick
//      below — read item.quality, advance it per your rules.
//   3. The engine's switch ignores unknown kind values, so combining your
//      pre-tick dispatch with a passthrough to the engine works cleanly.

// ----------------------------------------------------------------------------
// LiFx reimplementation. The 352-line engine body is too complex to port
// faithfully here, so this captures the SAFETY/GATING portion (which controls
// whether the engine even runs the per-item loop) and delegates the body.
// That gives you a clean editing surface for:
//   * Conditional disabling of furnace ticks (e.g., during certain hours)
//   * dt manipulation before processing
//   * Pre/post instrumentation logging
// Replace the engine delegate at the bottom of the function with a custom
// inventory walk when you need finer control.
// ----------------------------------------------------------------------------

namespace LifxImpl
{
	namespace WorkingFurnace
	{
		unsigned long long RecalcTick(void* self, float dt, char finalize)
		{
			// (1) Furnace-off gate — verbatim from engine.
			const auto state = *reinterpret_cast<int32_t*>(
				static_cast<char*>(self) + ::Engine::Off::Furnace_StateField);
			if (state == 0)
				return 0;

			// (2) dt clamp to [0, 1] (engine emits a warning if exceeded).
			const float maxDt = ::Engine::MaxTickDt();
			if (dt > maxDt) dt = maxDt;

			// (3) The 352-line body — inventory walk, per-item kind dispatch,
			//     temperature post-processing — is delegated to the engine.
			//     Resolve directly by RVA to avoid re-entering the hook.
			typedef unsigned long long (__fastcall *pfn)(void*, float, char);
			static const auto engineTick = ::Engine::AtRva<pfn>(0x1DCFF0);
			return engineTick(self, dt, finalize);
		}
	}
}

// ----------------------------------------------------------------------------
// Dispatch.
// ----------------------------------------------------------------------------
namespace { constexpr bool kUseLifxReimplementation = false; }

unsigned long long Hooks::WorkingFurnace::RecalcTick(void* self, float dt, char finalize)
{
	if constexpr (kUseLifxReimplementation)
		return LifxImpl::WorkingFurnace::RecalcTick(self, dt, finalize);
	return _WorkingFurnace_RecalcTick(self, dt, finalize);
}
