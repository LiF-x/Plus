#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.
	Declarations for the hook contributed by Pabluuz.

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
	Extra drops awarded only after a successful partial tunnel-dig operation.

	The vanilla material reward is left untouched. Once it succeeds, every
	configured item is rolled independently and awarded through the mining
	helper at +0x3A89E0, so one tunnelling action may award no configured item,
	one, or several.

	TerrainDeformer::AddMaterialsToInventory (+0x584120) is shared with other
	terrain-digging paths, so the detour is restricted to tunnelling by
	comparing _ReturnAddress() against the tunnel-only call site at +0x3ADEE9.

	Configured from lifxpluss.xml:
	    <tunnelDrops enabled="1" quality="100">
	        <item id="1234" chancePercent="5" minQuantity="1" maxQuantity="3" />
	    </tunnelDrops>
*/

#include "server/cm_server.h"

namespace tinyxml2 { class XMLElement; }

// TerrainDeformer::AddMaterialsToInventory (Win64 __fastcall). RVA 0x584120.
__CM_DECL_EXTERNAL(bool, __fastcall, _Engine_AddTunnelMaterialsToInventory,
                   U32 playerId, LPVOID inventoryContext, U8 selectedMaterial,
                   LPVOID materials, F32 qualityFactor, U32 containerId);

namespace Hooks
{
	namespace Engine
	{
		bool __fastcall OnAddTunnelMaterialsToInventory(
			U32 playerId, LPVOID inventoryContext, U8 selectedMaterial,
			LPVOID materials, F32 qualityFactor, U32 containerId);

		bool ConfigureTunnelDrops(const tinyxml2::XMLElement* root);
		void AttachTunnelDropHook();
		void DetachTunnelDropHook();
	}
}
