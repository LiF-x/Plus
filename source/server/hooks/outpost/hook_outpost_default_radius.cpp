/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_outpost_default_radius.h"

__CM_INSTATNTIATE(_Outpost_DefaultRadiusGetter);

// Seed with the engine's original hardcoded value (decompile of FUN_140187360
// shows `return 0x14;`). This keeps behaviour identical until something calls
// Lifx::setOutpostDefaultRadius.
std::atomic<uint32_t> Hooks::Outpost::g_defaultRadius{20};

uint64_t __fastcall Hooks::Outpost::DefaultRadiusGetter()
{
	return static_cast<uint64_t>(g_defaultRadius.load(std::memory_order_relaxed));
}
