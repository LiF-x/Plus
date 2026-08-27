#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

// Runtime dumper for the two NetEvent ABI facts that static RE couldn't
// recover (see docs/netevent_abi.md §"What stays open"). Tracks issue #54.
//
//   1. The ClassRep struct's pack/unpack/factory fn-pointer offsets — we
//      hook NetClassRep::add (FUN_140418C40) and snapshot the rep's first
//      0x80 bytes for every registered class. Cross-comparing 3-5 reps
//      reveals which offsets hold pointers consistently.
//   2. The aggregated event-sink at NetConnection+0x1F8 — we hook the
//      ServerUUIDEvent::send factory (FUN_1404E7370) and on first call
//      log the sink's vtable address.

#include "server/cm_server.h"

__CM_DECL_EXTERNAL(void, __fastcall, _NetClassRep_Add, void* rep);
__CM_DECL_EXTERNAL(void, __fastcall, _ServerUUIDEvent_Send, void* conn);

namespace Hooks
{
	namespace NetEvent
	{
		void NetClassRepAdd(void* rep);
		void ServerUUIDEventSend(void* conn);

		// Walk the existing NetClassRep list head-first and dump every
		// already-registered entry. Called once on attach because most
		// classes register via static initializers that ran long before
		// our Con_Init-time hook attached. List head = DAT_140BC00B0,
		// next-ptr at +0x50 on each rep (see docs/netevent_abi.md).
		void DumpExistingList();
	}
}
