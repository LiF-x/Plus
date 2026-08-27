/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "lifx_dispatcher.h"
#include "server/cm_server.h"
#include "server/api/t3d_console.h"
#include "server/hooks/dispatcher/dispatcher_client.h"
#include "server/hooks/netevent/sector_handoff_event.h"
#include "server/hooks/sector/client_redirect.h"
#include "server/hooks/sector/sector_edge_trigger.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	// LifxDispatcher::sendTo(<targetUuid>, <payloadText>)
	//   Sends the literal bytes of <payloadText> (UTF-8) as a forward
	//   frame to the named peer via the live dispatcher session.
	//   Returns 1 if queued, 0 if the session is down or the queue
	//   is full. Useful for smoke-testing the daemon link end-to-end.
	// LifxDispatcher::resolveSector(<id>)
	//   Issues a resolve_sector frame. The reply comes back asynchronously
	//   on the dispatcher reader thread and prints a single log line.
	//   Returns 1 if queued, 0 if not.
	S32 ResolveSector(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 2 || !argv[1])
		{
			Con::Warning("usage: LifxDispatcher::resolveSector(<world_sector_id>)");
			return 0;
		}
		const auto sid = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const bool ok = Hooks::Dispatcher::ResolveSector(sid,
			[sid](const Hooks::Dispatcher::ResolveResult& r) {
				if (r.known)
					Con::Echo("[lifx-dispatcher] resolveSector(%u) -> peer=%s",
					          (unsigned)r.world_sector_id, r.peer_id.c_str());
				else
					Con::Echo("[lifx-dispatcher] resolveSector(%u) -> unknown",
					          (unsigned)r.world_sector_id);
			});
		if (!ok)
			Con::Warning("[lifx-dispatcher] resolveSector(%u): not queued (session down?)", (unsigned)sid);
		return ok ? 1 : 0;
	}

	// LifxDispatcher::triggerHandoff(<charID>, <worldSectorId>, <worldX>, <worldY>)
	//   Resolves the destination peer for worldSectorId via the dispatcher,
	//   then forwards a v2 SectorHandoff payload with the provided fields.
	//   Returns 1 if the resolve was queued.
	S32 TriggerHandoff(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 5 || !argv[1] || !argv[2] || !argv[3] || !argv[4])
		{
			Con::Warning("usage: LifxDispatcher::triggerHandoff(<charID>, <worldSectorId>, <worldX>, <worldY>)");
			return 0;
		}
		const auto charID = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const auto sid    = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
		const float wx    = std::strtof(argv[3], nullptr);
		const float wy    = std::strtof(argv[4], nullptr);
		const bool ok = Hooks::SectorHandoff::ForwardToSector(charID, sid, wx, wy);
		if (!ok)
			Con::Warning("[lifx-dispatcher] triggerHandoff: resolve not queued (session down?)");
		return ok ? 1 : 0;
	}

	// LifxDispatcher::sectorEdgeStatus() — dumps the hook-captured Player,
	// its current pos, and per-trigger state (last-fire age, threshold,
	// target). Throwaway diagnostic for chunk 14 (#101); leave registered
	// for now so we can verify behaviour live.
	S32 SectorEdgeStatus(LPVOID /*obj*/, S32 /*argc*/, const char* /*argv*/[])
	{
		Hooks::SectorEdge::DumpStatus();
		return 1;
	}

	// LifxDispatcher::testTransfer(<peerWorldId>, "<host>", <port>)
	//   Fires Hooks::ClientRedirect::RedirectCapturedClient once. Use to
	//   verify the commandToClient pipeline without crossing an edge.
	S32 TestTransfer(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 4 || !argv[1] || !argv[2] || !argv[3]) {
			Con::Warning("usage: LifxDispatcher::testTransfer(<peerWorldId>, <host>, <port>)");
			return 0;
		}
		const auto pid  = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const std::string host = argv[2];
		const auto port = static_cast<std::uint16_t>(std::strtoul(argv[3], nullptr, 0));
		return Hooks::ClientRedirect::RedirectCapturedClient(pid, host, port) ? 1 : 0;
	}

	S32 SendTo(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 3 || !argv[1] || !argv[2])
		{
			Con::Warning("usage: LifxDispatcher::sendTo(<targetUuid>, <payloadText>)");
			return 0;
		}
		const char* target = argv[1];
		const char* text   = argv[2];
		std::vector<std::uint8_t> bytes(text, text + std::strlen(text));
		const bool ok = Hooks::Dispatcher::SendTo(std::string(target), bytes);
		Con::Echo("[lifx-dispatcher] sendTo target=%s bytes=%zu -> %s",
		          target, bytes.size(), ok ? "queued" : "DROPPED");
		return ok ? 1 : 0;
	}
}

void Lifx::Api::Dispatcher::Register()
{
	Con::AddCommand("LifxDispatcher", "sendTo", &SendTo,
	                "(string targetUuid, string payloadText) - send a forward "
	                "frame via the dispatcher daemon. Returns 1 on queue, 0 on drop.",
	                3, 3);
	Con::AddCommand("LifxDispatcher", "resolveSector", &ResolveSector,
	                "(int worldSectorId) - ask the dispatcher which peer "
	                "owns the sector. Reply logged asynchronously.",
	                2, 2);
	Con::AddCommand("LifxDispatcher", "triggerHandoff", &TriggerHandoff,
	                "(int charID, int worldSectorId, float worldX, float worldY) - "
	                "resolve the destination peer and forward a v2 SectorHandoff "
	                "payload. Returns 1 on resolve-queued, 0 otherwise.",
	                5, 5);
	Con::AddCommand("Lifx", "sectorEdgeStatus", &SectorEdgeStatus,
	                "() - dump hook-captured Player pos and per-trigger edge state (chunk 14, #101)",
	                1, 1);
	Con::AddCommand("LifxDispatcher", "testTransfer", &TestTransfer,
	                "(int peerWorldId, string host, int port) - manually fire client redirect (chunk 15b, #103)",
	                4, 4);
}
