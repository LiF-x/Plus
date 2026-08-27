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
	#145 — animation-name control for the Animal-derived hostile.

	Our hostile uses the PLAYER body (male.dts) on the engine's Animals::Animal
	class running the WOLF behaviour tree (aiBanditAggressive.xml). That tree drives
	NPCS::AnimatedNPC::setAnimation (0x2E2A90) with WOLF sequence names — Idle_Sleep,
	Idle_Sleep_Down/Up, Idle_Eat, Idle_Stand, Threatened, Attack_*, Death — none of
	which exist on male.dts. Each tick the engine logs "can't find animation %s",
	flooding the console (13k+ lines observed).

	This hook gates setAnimation: for animals WE spawned (registered via Register()),
	it SUPPRESSES requests male.dts can't satisfy — killing the flood. The full
	wolf->male sequence-name remap (the real "control animation names per derived
	class" deliverable) is layered on top once male.dts's own sequence names are
	extracted; the table lives here. Real wild animals (wolf.dts, NOT registered) are
	never touched, so their animations keep working.

	Signature: void __fastcall NPCS::AnimatedNPC::setAnimation(this, nameHandle, u8 flag).
*  =================================================================================== */

#include "server/cm_server.h"

// Original NPCS::AnimatedNPC::setAnimation (0x2E2A90). nameHandle is resolved to a
// char* internally; flag is a 1-byte mode. Detoured; the hook calls this for any
// non-hostile (real wild animals) so their anims are untouched.
__CM_DECL_EXTERNAL(void, __fastcall, _AnimatedNpc_SetAnimation, void* self, void* nameHandle, char flag);

namespace Hooks
{
	namespace AnimRemap
	{
		// Detour body installed on ANIMATED_NPC_SET_ANIMATION.
		void __fastcall OnSetAnimation(void* self, void* nameHandle, char flag);

		// Mark an Animal (the object pointer) as one of ours, so the hook governs
		// its animations. Called from Lifx::spawnHostile right after createAnimal.
		// Cheap atomic short-circuit makes the hook ~free until the first register.
		void Register(void* animal);

		// True if `animal` was Register()ed. Used by the hook to gate.
		bool IsHostile(void* animal);

		// #154 contact-frame hit: a committed swing queues its hit for ~the contact
		// frame instead of firing at swing-start (so the engine's weapon-node->hitbox
		// trace samples the sword mid-swing, hitting the real body part with timing that
		// matches the visual). OnHitTick drains the queue and is called every frame from
		// the vital process tick (main thread). Forget cancels any pending hit and clears
		// the bandit's combat state; call it when the bandit dies/despawns.
		void OnHitTick();
		void Forget(void* animal);
	}
}
