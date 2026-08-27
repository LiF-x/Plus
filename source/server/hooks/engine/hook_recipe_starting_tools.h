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
	StartingToolsID-aware recipe selection for processing devices.

	AbilityBaseWC::_onDoPerform asks CmRecipesManager for the first recipe
	matching the ability skill and target material. The stock lookup at RVA
	0x30BEE0 does not compare the recipe's StartingToolsID, so a custom
	high-capacity device receives its vanilla counterpart's recipe. For the
	device ObjectTypeIDs listed under <recipeStartingTools> the detour repeats
	the lookup with that one extra comparison.

	Configured from lifxpluss.xml:
	    <recipeStartingTools enabled="1">
	        <tool objectTypeId="1234" />
	    </recipeStartingTools>
*/

#include "server/cm_server.h"

namespace tinyxml2 { class XMLElement; }

// CmRecipesManager::findRecipeForMaterial (Win64 __fastcall). RVA 0x30BEE0.
__CM_DECL_EXTERNAL(LPVOID, __fastcall, _Engine_FindRecipeForMaterial,
                   LPVOID recipesManager, LPVOID sharedRecipeOut,
                   U32 skillTypeId, LPVOID materialObjectType);

namespace Hooks
{
	namespace Engine
	{
		LPVOID __fastcall OnFindRecipeForMaterial(
			LPVOID recipesManager, LPVOID sharedRecipeOut,
			U32 skillTypeId, LPVOID materialObjectType);

		bool ConfigureRecipeStartingTools(const tinyxml2::XMLElement* root);
		void AttachRecipeStartingToolsHook();
		void DetachRecipeStartingToolsHook();
	}
}
