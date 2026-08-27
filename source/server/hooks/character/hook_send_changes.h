#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	Hook on CmCharacterInfo::_sendChanges at RVA 0x1BC3D0.

	Signature:
	    void __fastcall _sendChanges(CmCharacterInfo* self, uint32_t mask, char sendNow);

	This is the server→client broadcast for character state. Every time the
	server wants to tell the client "stat X changed, here's the new value",
	this is called with `mask` indicating which fields changed.

	We want to know:
	  1. Which mask bits the engine sets when a character takes damage
	  2. Whether `+0x194` (HardHP) and `+0x19C` (SoftHP) on the self object
	     change values across this call (i.e. is HP part of the payload?)

	The hook snapshots a window of self before+after the call and echoes
	only fields that changed, plus the mask itself. We can then poke a
	field and call _sendChanges(self, <suspected bit>, 1) ourselves to
	push a fake update to the client.
*/

#include "server/cm_server.h"

#include <atomic>

__CM_DECL_EXTERNAL(void, __fastcall, _CharInfo_SendChanges,
                   void* self, unsigned mask, char sendNow);

namespace Hooks
{
	namespace CharInfoSendChanges
	{
		void Call(void* self, unsigned mask, char sendNow);

		extern std::atomic<unsigned long long> g_callCount;
		extern void* g_lastSelf;
		extern unsigned g_lastMask;

		// Bitmask OR of every mask value we've ever seen pass through.
		// Useful for "what mask bits does the engine actually use?".
		extern std::atomic<unsigned> g_observedMaskUnion;
	}
}
