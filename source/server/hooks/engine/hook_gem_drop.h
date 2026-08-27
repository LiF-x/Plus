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
	Gem drop hooks for the verified 1.4.4.5 dedicated-server build.

	Touches neither SQL nor inventory refresh. It changes the probability
	threshold at the gem-roll call site (+0x3A9258) and the selected itemId
	immediately before the normal item lookup (+0x3A931F). Both detours are on
	shared engine functions, so each is restricted to the gem path by comparing
	_ReturnAddress() against the recorded call-site return.

	Configured from lifxpluss.xml:
	    <gemDrops enabled="1" chancePercent="5">
	        <item id="481" weight="10" />
	    </gemDrops>
*/

#include "server/cm_server.h"

namespace tinyxml2 { class XMLElement; }

// Shared drop-probability helper called at +0x3A9258 (Win64 __fastcall).
__CM_DECL_EXTERNAL(F32, __fastcall, _Engine_ComputeDropChance,
                   U32 context, F32 baseChance, F32 bonusChance);

// Shared item-type lookup called at +0x3A931F (Win64 __fastcall).
__CM_DECL_EXTERNAL(LPVOID, __fastcall, _Engine_ItemTypeLookup,
                   LPVOID manager, U32 itemId);

namespace Hooks
{
	namespace Engine
	{
		F32 __fastcall OnComputeDropChance(U32 context, F32 baseChance, F32 bonusChance);
		LPVOID __fastcall OnItemTypeLookup(LPVOID manager, U32 itemId);

		bool ConfigureGemDrops(const tinyxml2::XMLElement* root);
		void AttachGemDropHooks();
		void DetachGemDropHooks();
	}
}
