/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_set_control_object.h"
#include "server/api/lifx_hostile.h"   // ReScopeGhostNpcs (A2a #125 reconnect persistence)

#include <atomic>

__CM_INSTATNTIATE(_GC_SetControlObject);

namespace
{
	std::atomic<void*> g_lastPlayer{nullptr};
	std::atomic<void*> g_lastConn{nullptr};
}

unsigned char Hooks::SetControlObject::Call(void* conn, void* obj)
{
	const unsigned char rc = _GC_SetControlObject(conn, obj);

	// Engine also calls this with obj==null to clear control (logged as
	// "reset control object to NULL"). Only capture real Player pointers.
	if (obj)
	{
		void* prev = g_lastPlayer.exchange(obj);
		g_lastConn.store(conn);
		if (prev != obj)
		{
			Con::Echo("[lifx-ctrl] captured controlled Player conn=%p obj=%p (was %p)",
			          conn, obj, prev);
		}
		// A2a #125: a client just got its control object (incl. after a reconnect).
		// setScopeAlways doesn't survive a fresh NetConnection, so re-scope our
		// ghosted NPCs now or a reconnecting client won't see them.
		Lifx::Api::Hostile::ReScopeGhostNpcs();
	}
	return rc;
}

void* Hooks::SetControlObject::LastControlledPlayer()
{
	return g_lastPlayer.load();
}

void* Hooks::SetControlObject::LastControlledConnection()
{
	return g_lastConn.load();
}
