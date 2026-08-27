#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

// Chunk 15a (#103): declarative world-grid model. Replaces chunk-14's
// per-edge half-plane triggers with a single description of THIS world's
// 3x3 sector grid plus any directly-adjacent neighbour worlds.
//
// On each tick the sector-edge driver asks WorldGrid::SectorForPos(x, y)
// which returns either:
//   - 0                       — outside any known grid (player off-world)
//   - one of this server's sectors — no-op, still in our domain
//   - a neighbour's sector    — fire ForwardToSector with the destination
//                               world's LOCAL coords (computed from the
//                               neighbour's declared origin + cellSize).
//
// Direction-based neighbour layout assumes the neighbour world's grid is
// stitched directly against ours at the named edge. North means +y, etc.

#include <cstdint>
#include <string>
#include <vector>

namespace Hooks
{
	namespace WorldGrid
	{
		enum class Direction { North, South, East, West };

		struct GridSpec
		{
			// Centre of cell (col=0, row=0) in world coords. Row 0 is the
			// SOUTH row, col 0 is the WEST column, so (originX, originY) is
			// the centre of the SW corner cell — NOT the centre of the
			// whole grid. Empirical layout for A's 442..450:
			//     origin=(-2042, -2042), cellSize=2044, gridSize=3x3
			float          originX = 0.f;
			float          originY = 0.f;
			float          cellSize = 0.f;  // metres, square cells
			std::uint32_t  cols = 0;
			std::uint32_t  rows = 0;
			std::uint32_t  firstSector = 0; // sector id at (col=0, row=0); row-major SW->NE
		};

		struct Resolved
		{
			std::uint32_t sectorId = 0;     // 0 if no grid covers (x, y)
			bool          isSelf = false;   // sector belongs to our own grid
			float         localX = 0.f;     // coords in the resolved grid's space
			float         localY = 0.f;
			// For neighbour resolves: the network endpoint the client
			// should connect to next. Empty when isSelf is true or when
			// the matching <neighbour> didn't carry peerHost/peerPort.
			std::string   peerHost;
			std::uint16_t peerPort = 0;
		};

		struct Neighbour
		{
			Direction       dir = Direction::East;
			GridSpec        grid{};
			std::string     peerHost;   // optional; "" if not set
			std::uint16_t   peerPort = 0;
		};

		// Replace the active world model. Self-grid is required (cols/rows > 0);
		// neighbours may be empty. Safe to call from DllMain — no logging.
		void Configure(GridSpec self, std::vector<Neighbour> neighbours);

		// Call after engine console is up (Server::Start) to dump the resolved
		// model to the log.
		void LogConfigured();

		// Resolve a world-space (x, y) against the self grid + neighbours.
		// Returns sectorId==0 if no grid covers the point.
		Resolved SectorForPos(float x, float y);
	}
}
