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
	Hook on FUN_1404dd100 (RVA 0x4DD100), the effect-table XML parser.

	This function is the sole consumer of 25 of the 26 distinct parameter
	tokens enumerated from data/cm_effects.xml (SPEED, HARD_HP_MAX, CONSUME,
	INCREASE_COEFF, BIND_TO_EFFECT_LIFETIME, …). The string-to-enum dispatch
	*is* the function body; whatever address it inhabits is by definition the
	engine's effect loader.

	Identification: `scripts/ghidra/LifxEffectsScan.java` cross-references
	every token against the binary's .rdata and ranks consumers by distinct-
	token fan-in. RVA 0x4DD100 has 25 distinct hits; the next-best function
	has 1. Full evidence in docs/effects_and_abilities.md.

	Prototype: per Ghidra decompile, no parameters and no return. The
	prologue saves five callee-saved regs, sets up a ~24KB stack frame via
	`__chkstk`, then runs `LEA RCX,[RSP+0x70]; CALL <ctor>` — i.e. it builds
	its own parser scratch object on the stack rather than receiving one
	from the caller. `void __fastcall(void)` matches what Microsoft x64 ABI
	demands when no args are passed.

	This first iteration is a pure passthrough that logs "parser fired" on
	entry and exit. That proves three things end-to-end without touching
	any effect data:
	  1. The Detours patch landed (verified by the post-commit byte read in
	     cm_server.cpp::AttachHooks — first byte should be 0xE9).
	  2. The seam fires during normal boot — establishing this is a viable
	     anchor for the second iteration (which will dump the parsed effect
	     table to a TorqueScript callback for live editing).
	  3. Forwarding to the trampoline preserves the engine's effect catalog
	     intact — checked by observing that `parser fired` is followed by
	     normal `cm_effects.xml` log lines from the engine.

	The next iteration will (a) read the parsed-effect array off the engine
	scratch object before it's torn down, and (b) invoke a TorqueScript
	callback `LifxOnEffectsParsed(count)` so scripts can override or extend
	the loaded set. That work is tracked separately; see the follow-up
	section in docs/effects_and_abilities.md.
*/

#include "server/cm_server.h"

__CM_DECL_EXTERNAL(void, __fastcall, _Effect_Parse, void);

namespace Hooks
{
	namespace Effect
	{
		void __fastcall Parse();
	}
}
