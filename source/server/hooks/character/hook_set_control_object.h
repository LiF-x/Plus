#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	Hook on GameConnection::setControlObject(ShapeBase*) — RE'd via the
	"GameConnection::setControlObject() -- set controlling client -- %s[%u] %u"
	log-string xref (issue #99). Captures the live Player* per connection
	so chunks 13/14 can read position without depending on the broken
	+0x1B44 charID stamp scan in FindPlayerByScan.

	Engine signature (Torque3D):
	    virtual bool GameConnection::setControlObject(ShapeBase* obj);

	MSVC x64 __fastcall: `this` in RCX, `obj` in RDX, return in AL.
*/

#include "server/cm_server.h"

#include <cstdint>

__CM_DECL_EXTERNAL(unsigned char, __fastcall, _GC_SetControlObject,
                   void* conn, void* obj);

namespace Hooks
{
	namespace SetControlObject
	{
		// Hook trampoline.
		unsigned char Call(void* conn, void* obj);

		// Most recent non-null Player* passed to setControlObject across
		// any connection. Atomic so the dispatcher thread can read it.
		// Chunk 14 will replace this single global with a proper
		// (charID -> Player*) map once we have a stable charID source.
		void* LastControlledPlayer();

		// Most recent GameConnection* paired with the captured Player.
		// Used by chunk 15b to address commandToClient at this conn.
		void* LastControlledConnection();
	}
}
