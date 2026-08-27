#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

// Dispatcher daemon client (issue #74, third chunk of #47).
//
// Opens a length-prefixed JSON session to lifxd: hello/hello_ack, then
// a unified read loop handling pong / delivery / forward_error frames
// plus a writer queue drained by the session thread for SendTo.
// Gated by <dispatcherEnabled>1</...> in lifxpluss.xml. Daemon being
// down or slow must NOT block server boot — the call runs on a detached
// std::thread with bounded socket timeouts.

#include "server/cm_server.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Hooks
{
	namespace Dispatcher
	{
		struct Config
		{
			std::string host;
			std::uint16_t port;
			std::uint32_t world_id;
			// Sector ids to auto-claim once the session is up
			// (chunk 10b, issue #91). Parsed from
			// <sectorClaims>442-450,500</...>.
			std::vector<std::uint32_t> sector_claims;
		};

		// Callback invoked from the session reader thread when the daemon
		// pushes a `delivery` frame. Receiver must not block; copy and
		// hand off if heavy work is needed.
		using DeliveryCallback = std::function<void(
			const std::string& from_peer_id,
			const std::vector<std::uint8_t>& payload)>;

		// Register the delivery callback. Replaces any prior callback.
		// Default is a Con::Echo stub. Safe to call before SpawnConnect.
		void SetDeliveryCallback(DeliveryCallback cb);

		// Enqueue a forward frame to be sent on the live session.
		// Returns true if queued, false if the session is down or the
		// queue is full (current cap kSendQueueMax). Non-blocking.
		bool SendTo(const std::string& target_peer_id,
		            const std::vector<std::uint8_t>& payload);

		// Result of an async ResolveSector call.
		struct ResolveResult
		{
			std::uint32_t world_sector_id = 0;
			bool          known           = false;
			std::string   peer_id;   // empty when !known
		};
		using ResolveCallback = std::function<void(const ResolveResult&)>;

		// Issue a resolve_sector frame and invoke `cb` when the reply
		// arrives. Returns true if the request was queued, false if the
		// session is down or the queue is full. Callback fires on the
		// session reader thread; must not block.
		bool ResolveSector(std::uint32_t world_sector_id, ResolveCallback cb);

		// Run the persistent connection loop forever (in practice: until
		// process exit). Connects, sends hello, reads hello_ack, then
		// drains the send queue and reads frames, sending a ping every
		// kPingIntervalMs of writer idleness. On any I/O error, closes
		// the socket, backs off exponentially (1s→30s cap), and reconnects.
		void RunForever(const Config& cfg);

		// Spawn RunForever on a detached thread so engine init isn't held
		// up by a slow or absent daemon. Safe to call from AttachHooks().
		void SpawnConnect(const Config& cfg);
	}
}
