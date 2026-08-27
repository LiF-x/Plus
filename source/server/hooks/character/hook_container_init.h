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

/*
	#145 Step 2 — live worn-loot tombstone.

	A freshly-created grave's loot container is tryInit'd EMPTY (the dead char's
	items haven't been moved into it yet), and CmServerInventoryContainer caches
	that empty state behind its +0x14 init guard — so a later DB move into the
	grave's container is invisible until a server restart reloads it.

	This hook detours CmServerInventoryContainer::tryInit (CONTAINER_TRYINIT) and,
	whenever a GRAVE container (ObjectTypeID 1070) loads, captures its in-memory
	pointer. After the SQL move (Lifx::dropBanditLoot), ReloadFreshestGraveContainer
	clears the captured container's +0x14 guard and re-runs the original tryInit, so
	the in-memory container repopulates from the DB live — no restart needed.

	Signature: U64 __fastcall CmServerInventoryContainer::tryInit(this).
*  =================================================================================== */

#include "server/cm_server.h"
#include <cstdint>

// Original CmServerInventoryContainer::tryInit (CONTAINER_TRYINIT). Detoured; the
// hook calls it unchanged for every container, then records grave containers.
__CM_DECL_EXTERNAL(unsigned long long, __fastcall, _Container_TryInit, void* container);

namespace Hooks
{
	namespace ContainerInit
	{
		// Detour body installed on CONTAINER_TRYINIT.
		unsigned long long __fastcall OnTryInit(void* container);

		// Monotonic counter, bumped every time a grave (type-1070) container is
		// captured. The death hook samples it before/after firing the death trigger:
		// a change means THIS death's grave container loaded synchronously and can be
		// filled immediately (no schedule, no race against the looter's first open).
		uint32_t GraveCaptureGen();

		// mID of the most-recently-captured grave container (its own container ID,
		// i.e. the grave's RootContainerID — the SQL move target).
		uint32_t FreshestGraveMid();

		// Force the most-recently-captured grave (type-1070) container to reload its
		// items from the DB (clear +0x14 init guard, re-run the original tryInit).
		// Call AFTER the SQL move has committed. expectMid != 0 logs a mismatch but
		// still reloads the freshest. Returns true if a container was reloaded.
		bool ReloadFreshestGraveContainer(uint32_t expectMid);
	}
}
