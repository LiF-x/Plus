#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.

	LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
	DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
	DEALINGS IN LIFX.
*  =================================================================================== */

#include "cm_wrappers.h"

namespace Lifx
{
	class Server
	{
		struct ServerConfig
		{
			U32 WorldID = 1;
			// Dispatcher peer identity, independent of the engine's WorldID.
			// Drives the deterministic peer UUID
			// (00000000-0000-0000-0000-{DispatcherWorldID:012X}). Set to a
			// distinct value per federated instance so the dispatcher can
			// route between them, even when several instances share a
			// MySQL DB and therefore the same engine WorldID. Defaults to
			// WorldID if omitted.
			U32 DispatcherWorldID = 0;

			// network and perfomance
			U32 MaxPlayers = 64;

			// torque logger
			U8 LogLevel = 0;
			bool SkipConsoleSQLLogging = false;
			bool UseExternalErrorLog = false;
			bool UseExternalSQLLog = false;

			// One-shot debug dumper for the NetEvent ABI (issue #54). When
			// true the LiFx DLL hooks NetClassRep::add + ServerUUIDEvent::send
			// and writes logs/netclassrep_dump.log. Keep off in production —
			// every class registration adds a few log lines on startup.
			bool DumpNetClassRep = false;

			// MVP SectorHandoff NetEvent registration (issue #58, first chunk
			// of #45). When true the LiFx DLL constructs + registers a custom
			// ClassRep at attach time. No wire payload yet — the slot-8 unpack
			// stub only logs. Off in production until the full handoff lands.
			bool RegisterSectorHandoff = false;

			// SectorHandoff auto-post (issue #62, third chunk of #45). When
			// true, the DLL hooks ServerUUIDEvent::send and posts one of our
			// SectorHandoffEvents on every non-NULL conn. Use as a round-trip
			// smoke test — verifies postNetEvent accepts our event without
			// crashing. Requires RegisterSectorHandoff.
			bool SectorHandoffAutoPost = false;

			// Sector handoff forwarding over the dispatcher daemon
			// (issue #82, chunk 7 of #47). When SectorHandoffTargetPeer
			// is non-empty AND auto-post fires, the 96-byte wire payload
			// is also pushed via Hooks::Dispatcher::SendTo to that peer.
			// When SectorHandoffReceiveLog is on, the DLL registers a
			// delivery callback that decodes 96-byte payloads and logs
			// the typed fields. Both require DispatcherEnabled to be
			// useful end-to-end.
			std::string SectorHandoffTargetPeer = "";
			bool        SectorHandoffReceiveLog = false;
			// When on, the delivery callback ALSO synthesizes a real
			// engine-allocated SectorHandoffEvent from the wire payload
			// (issue #84, chunk 8 of #47). Currently constructs the event,
			// verifies layout via read-back, then frees it. Chunk 9 will
			// route it into the engine drainer for real processing.
			bool        SectorHandoffInjectOnReceive = false;

			// Dispatcher daemon client (issue #74, third chunk of #47).
			// When enabled the DLL connects to lifxd at boot, completes
			// the JSON hello/ack handshake, and logs the assigned
			// peer_id. Off in production until the daemon is part of
			// the deployment.
			bool         DispatcherEnabled = false;
			std::string  DispatcherHost    = "127.0.0.1";
			std::uint16_t DispatcherPort   = 7400;
			// Comma-separated ranges + singles, e.g. "442-450,500".
			// Parsed at startup; passed to Dispatcher::Config so the
			// session-up handler auto-claims each id (issue #91).
			std::string  SectorClaims      = "";

			std::string LogSQLFN = "";
			std::string LogErrorFN = "";
		};

		static Server* instance_;
		static std::mutex instance_guard_;

		ServerConfig config_;

		bool is_init_;

		Server() : is_init_(false)
		{}
		~Server()
		{}

		void AttachHooks();
		void DetachHooks();

	public:

		Server(Server&) = delete;
		void operator=(const Server&) = delete;

		static Server& GetInstanceRef()
		{
			std::lock_guard<std::mutex> pg(instance_guard_); if (instance_ == nullptr) { instance_ = new Server(); } return *instance_;
		}

		ServerConfig const& GetConfig() { return config_; }

		// Initialize common server configuration and low-level procs
		void Init();

		// Finaly stop and destroy LiFx (must be done AFTER Stop())
		void Destroy();

		// Start main server proc with LiFx wrappers
		void Start();

		// Safely stop LiFx and CM server daemon
		void Stop();
	};
}

#define gServer Lifx::Server::GetInstanceRef()
