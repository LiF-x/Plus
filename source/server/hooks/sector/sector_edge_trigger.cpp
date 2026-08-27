/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "sector_edge_trigger.h"

#include "server/cm_server.h"
#include "server/api/t3d_console.h"
#include "server/hooks/character/hook_set_control_object.h"
#include "server/hooks/furnace/engine_internals.h"
#include "server/hooks/netevent/sector_handoff_event.h"
#include "server/hooks/sector/client_redirect.h"
#include "server/hooks/sector/world_grid.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace
{
	using Clock     = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	// charID hardcoded pending conn->charID RE (#86 follow-on).
	constexpr std::uint32_t kHardcodedCharID = 1;

	std::mutex      g_mu;
	TimePoint       g_lastTick{};
	std::uint32_t   g_lastSelfSector = 0;     // most recent sector we resolved as self
	std::uint32_t   g_lastFiredSector = 0;    // neighbour we most recently forwarded to;
	                                           // cleared when we return to a self sector
	TimePoint       g_lastFire{};
	float           g_lastX = 0.f;
	float           g_lastY = 0.f;
	std::uint32_t   g_lastResolvedSector = 0; // last resolved (self or neighbour)
	bool            g_lastIsSelf = true;

	constexpr auto kTickInterval = std::chrono::milliseconds(250); // ~4 Hz

	bool ReadPlayerPos(void* player, float& x, float& y, float& z)
	{
		if (!player) return false;
		const auto* base = static_cast<const unsigned char*>(player);
		x = *reinterpret_cast<const float*>(base + Engine::Off::Player_WorldPos + 0);
		y = *reinterpret_cast<const float*>(base + Engine::Off::Player_WorldPos + 4);
		z = *reinterpret_cast<const float*>(base + Engine::Off::Player_WorldPos + 8);
		return true;
	}
}

void Hooks::SectorEdge::OnTick()
{
	const auto now = Clock::now();
	{
		std::lock_guard<std::mutex> lk(g_mu);
		if (now - g_lastTick < kTickInterval) return;
		g_lastTick = now;
	}

	void* player = Hooks::SetControlObject::LastControlledPlayer();
	float x = 0.f, y = 0.f, z = 0.f;
	if (!ReadPlayerPos(player, x, y, z)) return;

	const auto resolved = Hooks::WorldGrid::SectorForPos(x, y);

	std::lock_guard<std::mutex> lk(g_mu);
	g_lastX = x;
	g_lastY = y;
	g_lastResolvedSector = resolved.sectorId;
	g_lastIsSelf         = resolved.isSelf;

	if (resolved.sectorId == 0) {
		// Off-world: nothing to do beyond updating status. Don't spam logs.
		return;
	}
	if (resolved.isSelf) {
		g_lastSelfSector  = resolved.sectorId;
		// Stepping back onto our own grid clears the fire latch so that
		// re-entering the same neighbour later WILL fire again.
		g_lastFiredSector = 0;
		return;
	}

	// In a neighbour's sector. Fire once per entry — only if we haven't
	// already forwarded to this exact sector (without leaving via a
	// self sector in between). Standing still in a neighbour sector
	// must NOT re-fire on the next tick.
	if (resolved.sectorId == g_lastFiredSector) return;
	g_lastFire        = now;
	g_lastFiredSector = resolved.sectorId;

	Con::Echo("[lifx-sector-edge] FIRE pos=(%.2f, %.2f) -> sector=%u local=(%.2f, %.2f) charID=%u",
	          x, y, resolved.sectorId, resolved.localX, resolved.localY,
	          (unsigned)kHardcodedCharID);

	const bool queued = Hooks::SectorHandoff::ForwardToSector(
		kHardcodedCharID, resolved.sectorId, resolved.localX, resolved.localY);
	if (!queued) {
		Con::Warning("[lifx-sector-edge] ForwardToSector returned false (dispatcher down or sector unclaimed?)");
	}

	// Chunk 15b (#103): also tell the client to open a secondary
	// GameConnection to the neighbour. peerWorldId derived from the
	// resolved sector id divided by an assumed 9-cell world; clamps
	// inside RedirectCapturedClient. No-op if the neighbour didn't
	// carry peerHost/peerPort.
	const auto peerWorldId = std::max<std::uint32_t>(
		1, (resolved.sectorId - 1) / 9 + 1);
	Hooks::ClientRedirect::RedirectCapturedClient(
		peerWorldId, resolved.peerHost, resolved.peerPort);
}

void Hooks::SectorEdge::DumpStatus()
{
	void* player = Hooks::SetControlObject::LastControlledPlayer();
	float x = 0, y = 0, z = 0;
	const bool gotPos = ReadPlayerPos(player, x, y, z);

	Con::Echo("[lifx-sector-edge] status: player=%p%s",
	          player, gotPos ? "" : " (no pos)");
	if (gotPos) {
		const auto r = Hooks::WorldGrid::SectorForPos(x, y);
		Con::Echo("[lifx-sector-edge]   pos=(%.3f, %.3f, %.3f) sector=%u self=%d",
		          x, y, z, r.sectorId, (int)r.isSelf);
	}

	std::lock_guard<std::mutex> lk(g_mu);
	const auto sinceFire = std::chrono::duration_cast<std::chrono::seconds>(
		Clock::now() - g_lastFire).count();
	Con::Echo("[lifx-sector-edge]   lastSelfSector=%u lastFiredSector=%u lastFireSecAgo=%lld",
	          g_lastSelfSector, g_lastFiredSector, (long long)sinceFire);
}
