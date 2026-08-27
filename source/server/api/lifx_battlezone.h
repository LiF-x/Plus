#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	LiFx battlezone utility commands exposed to TorqueScript (all under `Lifx::`).

	    Lifx::createBattleZone(type, geoIdInt, radius, name) -> int  new landId (-1 on failure)
	    Lifx::deleteBattleZone(landId)                       -> void remove a zone
	    Lifx::printBattleZones()                             -> void log all zones (engine passthrough)
	    Lifx::getBattleZones()                               -> string  newline rows "id geo radius type active"
	    Lifx::setBattleZoneActive(landId, active)            -> void arm/disarm containment live
	    Lifx::setBattleZoneExempt(charId, exempt)            -> void let a charId pass an armed boundary

	A battlezone is a Lands::BattleZoneLand (handle tag 4). With type==1 and
	active!=0 it confines players (engine snap-back). Other types are passive
	tracked regions. Zones are NOT DB-persisted — recreate them on boot (see
	dist/mods/Battlezones/battlezones.cs). Full internals: docs/battlezones.md.
*/

namespace Lifx
{
	namespace Api
	{
		namespace BattleZone
		{
			void Register();
		}
	}
}
