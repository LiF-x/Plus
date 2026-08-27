/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "world_grid.h"

#include "server/cm_server.h"
#include "server/api/t3d_console.h"

#include <cmath>
#include <mutex>

namespace
{
	std::mutex                                                          g_mu;
	Hooks::WorldGrid::GridSpec                                          g_self;
	std::vector<Hooks::WorldGrid::Neighbour>                            g_neighbours;

	const char* DirName(Hooks::WorldGrid::Direction d)
	{
		switch (d) {
			case Hooks::WorldGrid::Direction::North: return "north";
			case Hooks::WorldGrid::Direction::South: return "south";
			case Hooks::WorldGrid::Direction::East:  return "east";
			case Hooks::WorldGrid::Direction::West:  return "west";
		}
		return "?";
	}

	// Returns true if (x, y) falls within `g`. On hit, fills col/row.
	bool Locate(const Hooks::WorldGrid::GridSpec& g, float x, float y,
	            std::uint32_t& col, std::uint32_t& row)
	{
		if (g.cellSize <= 0.f || g.cols == 0 || g.rows == 0) return false;
		// Cell (col=0, row=0) is CENTERED at (originX, originY). Half-cell
		// either side of the centre is in-bounds. So the west edge of the
		// grid is originX - 0.5*cellSize, east edge is
		// originX + (cols - 0.5)*cellSize, etc.
		const float halfCell = 0.5f * g.cellSize;
		const float west  = g.originX - halfCell;
		const float south = g.originY - halfCell;
		const float east  = west + g.cols * g.cellSize;
		const float north = south + g.rows * g.cellSize;
		if (x < west || x >= east || y < south || y >= north) return false;
		col = static_cast<std::uint32_t>((x - west) / g.cellSize);
		row = static_cast<std::uint32_t>((y - south) / g.cellSize);
		if (col >= g.cols) col = g.cols - 1;
		if (row >= g.rows) row = g.rows - 1;
		return true;
	}
}

void Hooks::WorldGrid::Configure(GridSpec self, std::vector<Neighbour> neighbours)
{
	std::lock_guard<std::mutex> lk(g_mu);
	g_self = self;
	g_neighbours = std::move(neighbours);
}

void Hooks::WorldGrid::LogConfigured()
{
	std::lock_guard<std::mutex> lk(g_mu);
	if (g_self.cellSize <= 0.f || g_self.cols == 0 || g_self.rows == 0) {
		Con::Echo("[lifx-world-grid] no self grid configured (handoffs disabled)");
		return;
	}
	Con::Echo("[lifx-world-grid] self: origin=(%.2f, %.2f) cellSize=%.2f grid=%ux%u firstSector=%u",
	          g_self.originX, g_self.originY, g_self.cellSize,
	          g_self.cols, g_self.rows, g_self.firstSector);
	for (const auto& nb : g_neighbours) {
		Con::Echo("[lifx-world-grid]   neighbour %s: origin=(%.2f, %.2f) cellSize=%.2f grid=%ux%u firstSector=%u peer=%s:%u",
		          DirName(nb.dir), nb.grid.originX, nb.grid.originY, nb.grid.cellSize,
		          nb.grid.cols, nb.grid.rows, nb.grid.firstSector,
		          nb.peerHost.empty() ? "(unset)" : nb.peerHost.c_str(),
		          (unsigned)nb.peerPort);
	}
}

Hooks::WorldGrid::Resolved Hooks::WorldGrid::SectorForPos(float x, float y)
{
	std::lock_guard<std::mutex> lk(g_mu);
	Resolved r;

	std::uint32_t col = 0, row = 0;
	if (Locate(g_self, x, y, col, row)) {
		r.sectorId = g_self.firstSector + row * g_self.cols + col;
		r.isSelf   = true;
		r.localX   = x;
		r.localY   = y;
		return r;
	}

	// Convert THIS world's (x, y) into each neighbour's local coords and
	// see if the neighbour's grid covers it. We assume the neighbour's
	// grid is stitched directly against ours at the named edge, with the
	// same cellSize on each side. The neighbour's local coords are
	// derived by translating across the shared edge: the player who just
	// stepped one cell east of our east edge is one cell west of the
	// neighbour's west edge, and the neighbour's local origin describes
	// where its (col=0, row=0) sits in its own coord system.
	const float halfSelfCell = 0.5f * g_self.cellSize;
	const float selfWest  = g_self.originX - halfSelfCell;
	const float selfSouth = g_self.originY - halfSelfCell;
	const float selfEast  = selfWest  + g_self.cols * g_self.cellSize;
	const float selfNorth = selfSouth + g_self.rows * g_self.cellSize;
	const float selfHeight = g_self.rows * g_self.cellSize;
	const float selfWidth  = g_self.cols * g_self.cellSize;

	for (const auto& nb : g_neighbours) {
		const auto& n = nb.grid;
		float lx = x, ly = y;
		switch (nb.dir) {
			case Direction::East:  lx = x - selfEast  + (n.originX - 0.5f * n.cellSize); break;
			case Direction::West:  lx = x - selfWest  + (n.originX - 0.5f * n.cellSize) + n.cols * n.cellSize - selfWidth; break;
			case Direction::North: ly = y - selfNorth + (n.originY - 0.5f * n.cellSize); break;
			case Direction::South: ly = y - selfSouth + (n.originY - 0.5f * n.cellSize) + n.rows * n.cellSize - selfHeight; break;
		}
		std::uint32_t nc = 0, nr = 0;
		if (Locate(n, lx, ly, nc, nr)) {
			r.sectorId = n.firstSector + nr * n.cols + nc;
			r.isSelf   = false;
			r.localX   = lx;
			r.localY   = ly;
			r.peerHost = nb.peerHost;
			r.peerPort = nb.peerPort;
			return r;
		}
	}
	return r;
}
