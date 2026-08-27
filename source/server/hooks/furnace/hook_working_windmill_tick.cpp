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

#include "hook_working_windmill_tick.h"
#include "engine_internals.h"

__CM_INSTATNTIATE(_WorkingWindmill_RecalcTick);

// ============================================================================
// ENGINE REFERENCE — WorkingWindmill::recalcTick @ RVA 0x1DFF20
// Verbatim Ghidra decompile. Trivial: runs a helper that touches global state
// (likely advances a grindstone counter), then stamps the current time into
// the windmill's "last tick" field at +0x08.
// ----------------------------------------------------------------------------
// undefined8 FUN_1401dff20(longlong param_1)
// {
//   undefined4 uVar1;
//   FUN_1401dfb20();                  // ::Engine::Windmill_TickHelper()
//   uVar1 = FUN_1405147a0();          // ::Engine::ServerTime_Now()
//   *(undefined4 *)(param_1 + 8) = uVar1;
//   return 0;
// }
// ============================================================================

namespace LifxImpl
{
	namespace WorkingWindmill
	{
		unsigned long long RecalcTick(void* self, float /*dt*/, char /*finalize*/)
		{
			::Engine::Windmill_TickHelper();
			*reinterpret_cast<uint32_t*>(static_cast<char*>(self) + ::Engine::Off::Windmill_LastTick)
				= ::Engine::ServerTime_Now();
			return 0;
		}
	}
}

namespace { constexpr bool kUseLifxReimplementation = false; }

unsigned long long Hooks::WorkingWindmill::RecalcTick(void* self, float dt, char finalize)
{
	if constexpr (kUseLifxReimplementation)
		return LifxImpl::WorkingWindmill::RecalcTick(self, dt, finalize);
	return _WorkingWindmill_RecalcTick(self, dt, finalize);
}
