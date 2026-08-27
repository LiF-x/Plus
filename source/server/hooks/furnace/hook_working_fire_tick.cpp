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

#include "hook_working_fire_tick.h"
#include "engine_internals.h"

__CM_INSTATNTIATE(_WorkingFire_RecalcTick);

// ============================================================================
// ENGINE REFERENCE — WorkingFire::recalcTick @ RVA 0x1DB650
// Verbatim Ghidra decompile. Trivial: returns whether the fire's state-alt
// timestamp is in the past (i.e., the fire's expiry time has elapsed).
// ----------------------------------------------------------------------------
// bool FUN_1401db650(longlong param_1)
// {
//   uint uVar1;
//   uVar1 = FUN_1405147a0();             // ::Engine::ServerTime_Now()
//   return *(uint *)(param_1 + 0x28) < uVar1;
// }
// ============================================================================

// ----------------------------------------------------------------------------
// LiFx C++ reimplementation of the engine's WorkingFire::recalcTick.
//
// Mirrors the engine line-for-line so behavior is byte-equivalent when this
// branch is taken. Edit freely to extend or replace; the passthrough below
// remains available via the kUseLifxReimplementation toggle.
// ----------------------------------------------------------------------------
namespace LifxImpl
{
	namespace WorkingFire
	{
		unsigned long long RecalcTick(void* self, float /*dt*/, char /*finalize*/)
		{
			// Engine: return self->stateAlt < ServerTime::Now();
			const auto state = *reinterpret_cast<uint32_t*>(
				static_cast<char*>(self) + ::Engine::Off::Furnace_StateAlt);
			const auto now = ::Engine::ServerTime_Now();
			// Original returns `bool`; the implicit conversion to our wider
			// uniform return type sets RAX's low byte correctly.
			return state < now;
		}
	}
}

// ----------------------------------------------------------------------------
// Dispatch. Default = passthrough (byte-equivalent to the unmodified engine).
// Flip kUseLifxReimplementation to `true` to route through LifxImpl above
// when you want to extend the behavior. `if constexpr` ensures the unused
// branch is dead-code-eliminated, so there's no runtime cost either way.
// ----------------------------------------------------------------------------
namespace { constexpr bool kUseLifxReimplementation = true; }

unsigned long long Hooks::WorkingFire::RecalcTick(void* self, float dt, char finalize)
{
	if constexpr (kUseLifxReimplementation)
		return LifxImpl::WorkingFire::RecalcTick(self, dt, finalize);
	return _WorkingFire_RecalcTick(self, dt, finalize);
}
