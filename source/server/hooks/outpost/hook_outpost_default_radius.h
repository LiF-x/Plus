#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	Hook on FUN_140187360 (RVA 0x187360), a single-instruction function whose
	original body is literally `return 20;`. It supplies the third argument
	(radius) of `call p_createOutpostLandAndClaim(%u, %u, %u);` issued by
	Lands::DB::CreateOutpostLandAndClaim — i.e. the radius written into
	`guild_lands.Radius` when a brand-new outpost is created.

	Hooking it lets LiFx serve any uint we want as the default outpost
	radius. The hooked value is read from g_defaultRadius (atomic). At
	startup g_defaultRadius is seeded with 20 to match the engine's
	hardcoded value, so behaviour is unchanged until Lifx::setOutpostDefaultRadius
	is called.

	Scope: this is the *default* applied to NEW outposts. It does not
	retroactively change existing outposts — that needs a separate path
	through Lands::Manager::changeGuildLandRadius (RVA 0x2D0F00), tracked
	in a follow-up issue.
*/

#include "server/cm_server.h"

#include <atomic>
#include <cstdint>

__CM_DECL_EXTERNAL(uint64_t, __fastcall, _Outpost_DefaultRadiusGetter, void);

namespace Hooks
{
	namespace Outpost
	{
		uint64_t __fastcall DefaultRadiusGetter();

		extern std::atomic<uint32_t> g_defaultRadius;
	}
}
