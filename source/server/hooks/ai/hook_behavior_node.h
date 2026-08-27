#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

/*
	Adds custom AI behavior-tree node primitives to the engine's node factory,
	proving LiFx can extend the node catalog via DLL injection alone (no exe
	patching). Registered classes:
	  - LifxLogNode (issue #119)        — logs on tick, always succeeds (PoC).
	  - TimeOfDayBetween (issue #121)   — succeeds when the in-game hour is in
	      value="start end" (wraps past midnight); has a real loadFromXml.
	  - IsNight (issue #121)            — TimeOfDayBetween with the night range
	      (20..6) pre-baked (no value needed).
	  - GoToPoint (issue #123)          — first action node: pathfinds to
	      value="x y z". Cloned from GoToPosition (reuses its process), with a
	      coord-parsing loadFromXml.

	The engine's node factory uses a prototype pattern: register one INode*
	under an XML class name and the engine clones it for every parsed
	<node class="..."/>. We hook the per-tree XML loader (RVA 0x153B80, logs
	"Loading AI behaviour from [%s]") and register "LifxLogNode" the first
	time it runs (retried until the built-in "Stopped" template is present),
	before that tree's nodes are parsed. This loader is used by BOTH the boot
	load path (onServerCreated) and the manual reloadBehaviorXml() console
	command, so a single hook covers both. The original loader then proceeds
	unchanged.

	The clone strategy is layout-agnostic: we never assume a node's field
	layout. We clone an existing simple leaf ("Stopped"), snapshot its vtable,
	and patch only slot 2 (clone) and slot 3 (process). Our clone delegates to
	the template's real clone (correct deep copy) then retargets the result's
	vtable to ours. See ABI_NOTES.md in this directory for the full RE.
*/

#include "server/cm_server.h"

// Per-tree behavior-XML loader. Constructs a BehaviorTree and loads it from
// the named file; logs "Loading AI behaviour from [%s]". Returns `self`.
__CM_DECL_EXTERNAL(void*, __fastcall, _Ai_LoadBehaviorXml, void* self, const char* fileName);

namespace Hooks
{
	namespace AI
	{
		void* __fastcall LoadBehaviorXml(void* self, const char* fileName);
	}
}
