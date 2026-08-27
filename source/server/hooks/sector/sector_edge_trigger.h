#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

// Chunk 14 (#101) introduced this driver as a per-edge half-plane detector.
// Chunk 15a (#103) repurposed it as a sector-change detector built on top
// of Hooks::WorldGrid::SectorForPos. The XML config flipped from
// <sectorEdgeTriggers> (per-edge list) to <worldGrid> + <worldNeighbours>
// (declarative grid). The tick wiring and ForwardToSector path are
// unchanged — only the trigger condition.

namespace Hooks
{
	namespace SectorEdge
	{
		// Called from VitalParams::ProcessTick. Internal time + state guard
		// (~4 Hz max). No-op if WorldGrid has no self grid configured or
		// no captured Player.
		void OnTick();

		// Diagnostic dump for Lifx::sectorEdgeStatus.
		void DumpStatus();
	}
}
