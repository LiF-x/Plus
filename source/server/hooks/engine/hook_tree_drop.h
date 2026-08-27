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
	Per-tree movable-object drop override.

	Vanilla code at +0x3716D0 selects ObjectTypeID 626 (hardwood) or 653
	(softwood) and stores it in [rbp+0x110]; the tree species byte is still
	live at [rbp+0x120]. This hook does NOT use Detours — it replaces the
	26-byte selector with an absolute jump into a generated trampoline that
	preserves the vanilla selection and then applies only the XML entries
	matching that species.

	Because it patches bytes directly rather than going through Detours, it is
	safe to attach outside a DetourTransactionBegin/Commit block.

	Configured from lifxpluss.xml:
	    <treeDrops enabled="1">
	        <drop tree="oak" itemId="1234" />
	    </treeDrops>
*/

#include "server/cm_server.h"

namespace tinyxml2 { class XMLElement; }

namespace Hooks
{
	namespace Engine
	{
		bool ConfigureTreeDrops(const tinyxml2::XMLElement* root);
		void AttachTreeDropHook();
		void DetachTreeDropHook();
	}
}
