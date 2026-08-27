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
	#145 — death -> Player tombstone redirect for the Animal-derived hostile.

	RE (2026-06-25): the central death router (0x3BE890) RTTI-dispatches a dying
	entity: Animals::Animal -> Animals::Animal::createCorpse (0x18A370, spawns the
	skinnable carcass from AnimalData.rawCorpseObjectTypeID @ datablock+0x8478);
	Player -> charStats->vtbl[0x130](charStats, 0) (the real death -> lootstone +
	worn-loot trigger). NPCS::PlayerBased (NPCDecorative) -> its own vtbl[0x60],
	which is why it gets a tombstone for free.

	We hook createCorpse (the single carcass chokepoint). For animals WE spawned
	(gated via Hooks::AnimRemap::IsHostile), instead of the carcass we invoke the
	SAME Player death trigger the engine uses (charStats->vtbl[0x130]) — yielding the
	Player tombstone + worn-loot pipeline — then run createCorpse's manager-cleanup
	tail and skip the carcass. A thread-local re-entry guard prevents recursion if the
	trigger routes back through the death router. Real wild animals (not registered)
	get the stock carcass untouched.

	Signature: void __fastcall Animals::Animal::createCorpse(this).
*  =================================================================================== */

#include "server/cm_server.h"

// Original Animals::Animal::createCorpse (0x18A370). Detoured; called unchanged for
// any non-hostile animal so wild wolves/bears still drop skinnable carcasses.
__CM_DECL_EXTERNAL(void, __fastcall, _Animal_CreateCorpse, void* animal);

namespace Hooks
{
	namespace AnimalDeath
	{
		// Detour body installed on ANIMAL_CREATE_CORPSE.
		void __fastcall OnCreateCorpse(void* animal);
	}
}
