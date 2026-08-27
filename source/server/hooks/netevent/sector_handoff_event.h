#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

// SectorHandoff MVP — issue #58 (first chunk of #45 / #50).
//
// At attach time, builds a 0x80-byte ClassRep (cloned from
// ServerUUIDEvent's template at runtime, with our own 9-slot vtable
// overriding slot 7 = create() and slot 8 = unpack stub) and calls
// NetClassRep::add. Proves the registration plumbing without sending
// or receiving any actual events yet.
//
// See docs/netevent_receive_path.md (recipe) and docs/netevent_abi.md
// (struct + vtable layout the registration walks).

#include "server/cm_server.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Hooks
{
	namespace SectorHandoff
	{
		// Construct + register our ClassRep with the engine. Idempotent:
		// no-op if called twice. Returns false if module-base resolution
		// or NetClassRep::add lookup fails.
		bool Register();

		// Allocate a fresh SectorHandoffEvent via our slot-7 create() and
		// hand it to `conn`'s NetConnection::postNetEvent vfn (via the
		// embedded GameConnection-subobject vtable at conn + 0x1F8). Logs
		// the engine's return code. Safe to call with conn=nullptr — no-ops.
		// Returns the engine's success byte (1 = accepted, 0 = dropped).
		//
		// Called from the dumper's ServerUUIDEventSend hook when the
		// `<sectorHandoffAutoPost>` config flag is on — we don't install a
		// separate hook on the same RVA because Detours can't stack two
		// attaches on the same fn in one transaction (corrupts trampoline
		// chain, blew the stack — issue #62 follow-up).
		std::uint8_t PostTo(void* conn);

		// Build a sample SectorHandoffEvent payload in heap memory and
		// forward the 96 wire bytes via Hooks::Dispatcher::SendTo. Does
		// NOT call postNetEvent — bypasses the engine drainer entirely
		// so a real client connection cannot be kicked by our half-RE'd
		// vt[10] classId-write stub returning 0. Used by the auto-post
		// trigger hook when a real client connection is present.
		void TriggerSampleForward();

		// Decode a 96-byte wire payload (the 24 u32s our pack() emits,
		// starting at struct offset +0x48) and log the typed fields.
		// Wrong-sized payloads are logged and ignored. Used by the
		// delivery callback registered when SectorHandoffReceiveLog is on.
		void DecodeAndLog(const std::string& from_peer_id,
		                  const std::vector<std::uint8_t>& payload);

		// Synthesize a SectorHandoffEvent on the receive side from a
		// 96-byte wire payload: allocate via slot-7 create(), memcpy
		// payload to evt+0x48, log roundtrip read-back. Currently the
		// event is freed at the end of the call — driving the engine
		// drainer to actually PROCESS the event is chunk 9 (needs RE
		// of the post-unpack process slot first). Wrong-sized payloads
		// are logged and ignored.
		void InjectFromWire(const std::string& from_peer_id,
		                    const std::vector<std::uint8_t>& payload);

		// Resolve `world_sector_id` against the dispatcher, then on
		// success build a v2 wire payload populated with the supplied
		// char_id / world coordinates (plus placeholder vitals + sample
		// src/dst UUIDs) and forward to the resolved peer. Async — the
		// resolve reply lands on the dispatcher reader thread. Returns
		// true if the resolve request was queued. Used by the chunk-12
		// LifxDispatcher::triggerHandoff TS command and (later) the
		// edge-trigger hook (chunk 13).
		bool ForwardToSector(std::uint32_t char_id,
		                     std::uint32_t world_sector_id,
		                     float world_x,
		                     float world_y);
	}
}
