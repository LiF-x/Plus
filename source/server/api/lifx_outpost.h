#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	LiFx outpost / guild-land utility commands exposed to TorqueScript.

	Currently exposed (all under the `Lifx::` namespace in TorqueScript):

	    Lifx::getOutpostDefaultRadius()                    -> int   current default radius
	    Lifx::setOutpostDefaultRadius(radius)              -> void  override for new outposts
	    Lifx::setOutpostRadius(landID, radius)             -> void  change an existing outpost's radius
	    Lifx::setOutpostProductionType(unmovableID, type)  -> void  retarget existing outpost production
	    Lifx::dumpOutposts()                               -> void  list every live outpost

	The two `default`/`set` radius commands target different surfaces:
	`setOutpostDefaultRadius` only affects NEW outposts; `setOutpostRadius`
	mutates an EXISTING guild_lands row and triggers the engine's monument
	rebuild. See docs/outposts.md for the leverage points behind each.
*/

namespace Lifx
{
	namespace Api
	{
		namespace Outpost
		{
			void Register();
		}
	}
}
