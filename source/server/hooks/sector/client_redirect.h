#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

// Chunk 15b (#103 part 2): drive the client's existing
// clientCmdIrsp(1, serverId, addr) RPC from the server-side DLL so that
// crossing into a neighbour world opens a SECONDARY GameConnection on the
// client to that neighbour. The client's TS already handles
// initiateNewConnection — we just need to invoke commandToClient with the
// right args.
//
// Primary->secondary swap (so the player actually starts playing on the
// neighbour) is chunk 15c.

#include <cstdint>
#include <string>

namespace Hooks
{
	namespace ClientRedirect
	{
		// Issue commandToClient(client, 'Irsp', 1, peerWorldId, "IP:host:port")
		// against the hook-captured GameConnection. peerWorldId is used as
		// the client-side secondary-connection slot ([1..49]); pass a stable
		// per-neighbour id (we use the neighbour's worldID for simplicity).
		// Logs + returns false if the conn is null, no host configured, or
		// the TS eval surface isn't ready.
		bool RedirectCapturedClient(std::uint32_t peerWorldId,
		                            const std::string& host,
		                            std::uint16_t port);
	}
}
