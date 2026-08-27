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

#include "hook_working_trap_tick.h"
#include "engine_internals.h"

__CM_INSTATNTIATE(_WorkingTrap_RecalcTick);

// ============================================================================
// ENGINE REFERENCE — WorkingTrap::recalcTick @ RVA 0x1DEE90
// Verbatim Ghidra decompile lives at /tmp/lifx_ghidra/decompile/rt_1DEE90.c
// (213 lines). It implements a sizable trap state machine: bait consumption,
// rearming, animal-catch resolution, weapon-data lookup, animal spawning at
// the trap's location, and damage scaling.
//
// Top-level control flow:
//
//   if (self->state == 0) return 0;             // disarmed/empty
//   now = ServerTime::Now();
//   ... walks the trap's "watched targets" list ...
//   ... runs the weapon-data lookup (FUN_1401dee20 etc.) ...
//   ... iterates candidate captures and rolls outcomes ...
//   ... mutates inventory (bait consumed, captured creature added) ...
// ============================================================================

namespace LifxImpl
{
	namespace WorkingTrap
	{
		// The full body of recalcTick is too entangled with engine internals
		// (specific weapon-data structures, animal-spawn helpers, the trap's
		// per-instance state) to port faithfully here without a much deeper
		// decompile pass. The single-line gate at the top is what's worth
		// exposing for typical extensions (e.g., disable all traps in a
		// region, throttle traps during certain hours, log every fire).
		//
		// To extend further, replace the engine delegate below with your own
		// inventory walk + animal-spawn logic. The relevant helpers are in
		// engine_internals.h (ServerTime_Now, the Inv_* family).
		unsigned long long RecalcTick(void* self, float dt, char finalize)
		{
			// Gate — edit to add custom "should this trap tick run?" rules.
			const auto state = *reinterpret_cast<int32_t*>(
				static_cast<char*>(self) + ::Engine::Off::Furnace_StateAlt);
			if (state == 0)
				return 0;

			// Inner logic delegated to the engine — see notes above.
			typedef unsigned long long (__fastcall *pfn)(void*, float, char);
			static const auto engineTick = ::Engine::AtRva<pfn>(0x1DEE90);
			return engineTick(self, dt, finalize);
		}
	}
}

namespace { constexpr bool kUseLifxReimplementation = false; }

unsigned long long Hooks::WorkingTrap::RecalcTick(void* self, float dt, char finalize)
{
	if constexpr (kUseLifxReimplementation)
		return LifxImpl::WorkingTrap::RecalcTick(self, dt, finalize);
	return _WorkingTrap_RecalcTick(self, dt, finalize);
}
