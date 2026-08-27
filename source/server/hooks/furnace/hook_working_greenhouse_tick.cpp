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

#include "hook_working_greenhouse_tick.h"
#include "engine_internals.h"

__CM_INSTATNTIATE(_WorkingGreenhouse_RecalcTick);

// ============================================================================
// ENGINE REFERENCE — WorkingGreenhouse::recalcTick @ RVA 0x1DEA70
//
// Distilled control flow (full verbatim decompile at
// /tmp/lifx_ghidra/decompile/rt_1DEA70.c):
//
//   if (self->stateAlt <= now() && self->stateAlt != 0) {
//       self->stateAlt = 0;
//       getContents(self, &contents);
//       if (contents != NULL) {
//           if (FUN_1400c9920() == 2)
//               FUN_1400cb7e0(contents, 1, 1, ...);
//           // Construct a std::function<void(GameConnection*)> on the stack
//           // and pass it to Furnace_Visit. The lambda captures `self` and
//           // its body lives in the binary at a vftable we haven't mapped
//           // here — see notes below.
//           Furnace_Visit(self, &lambdaObj);
//       }
//   }
//   return 0;
// ============================================================================

namespace LifxImpl
{
	namespace WorkingGreenhouse
	{
		// Reimplementation notes:
		//
		// The TOP of greenhouse's tick is a clean "is it time to update?" gate
		// — that's the part most worth exposing for extension (e.g., add a
		// time-of-day check, a region check, a daily-quota check, etc.).
		//
		// The BOTTOM does the actual plant growth via a std::function lambda
		// captured at compile time. The lambda's body lives in the engine's
		// .text and is dispatched through a per-class vftable embedded as a
		// local in the engine's stack frame. Reproducing that exactly in C++
		// without a much deeper port of the engine's plant-spawn helpers
		// (FUN_1400c9920, FUN_1400cb7e0, and the lambda body itself) isn't
		// practical here.
		//
		// So this reimplementation handles the gate explicitly and delegates
		// the body. Replace the delegate with your own plant-growth logic
		// when you have a specific extension in mind.
		unsigned long long RecalcTick(void* self, float dt, char finalize)
		{
			auto* stateAlt = reinterpret_cast<uint32_t*>(
				static_cast<char*>(self) + ::Engine::Off::Furnace_StateAlt);
			const auto now = ::Engine::ServerTime_Now();

			// Gate — edit this to change when greenhouse processing fires.
			if (*stateAlt > now || *stateAlt == 0)
				return 0;

			// The engine internally re-runs this gate, then clears stateAlt
			// and proceeds. We delegate to keep behavior identical.
			typedef unsigned long long (__fastcall *pfn)(void*, float, char);
			static const auto engineTick = ::Engine::AtRva<pfn>(0x1DEA70);
			return engineTick(self, dt, finalize);
		}
	}
}

namespace { constexpr bool kUseLifxReimplementation = false; }

unsigned long long Hooks::WorkingGreenhouse::RecalcTick(void* self, float dt, char finalize)
{
	if constexpr (kUseLifxReimplementation)
		return LifxImpl::WorkingGreenhouse::RecalcTick(self, dt, finalize);
	return _WorkingGreenhouse_RecalcTick(self, dt, finalize);
}
