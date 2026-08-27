#pragma once

#include <cstdint>

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
	A2a character-backed hostile NPC (issue #125).

	The end goal: spawn an NPCS::NPCDecorative / NPCS::PlayerBased (which carries
	BOTH the player character layout @+0xAA8 AND the AI layout @+0x24B8/+0x24C0),
	bind a character to it, equip a loadout, and give it an aggressive behaviour
	tree — so equip-render and death->corpse->worn-loot ride the engine's own
	Player pipelines.

	Phase 0 (this file, first increment): a SAFE, read-only probe that confirms
	the RE'd equipment-registry layout on charStats. The equipment for a character
	is reached via an FNV hash-map keyed on charStats (NOT a fixed-offset
	sub-object): the registry fields live on charStats, and the entry resolves to
	a CmPlayerEquipment. Before we touch an NPC (which needs the still-un-RE'd
	character-bind path), we validate those offsets against a known-good, real,
	connected player whose equipment registry is definitely populated.

	    Lifx::dumpCharEquip(charID)  -> read-only dump of the equipment-registry
	                                    state on a connected player's charStats

	Next increments (tracked on #125):
	  - RE the character-bind path (how a connecting player's charStats@+0xAA8 gets
	    its CmCharacterInfo + equipment registry populated from a charID), so we can
	    bind a synthetic CreateTestCharacter row to a spawned NPC.
	  - Lifx::spawnHostile(x,y,z,loadout): create NPCDecorative -> bind character ->
	    equip -> setBehavior.
*/

namespace Lifx
{
	namespace Api
	{
		namespace Hostile
		{
			// Register the A2a hostile-NPC commands with Torque's console.
			// Call once from Hooks::Engine::ConsoleInit alongside the other
			// Lifx::Api::*::Register() calls.
			void Register();

			// #169 — character-bind a freshly spawned hostile Animal so its death
			// drops a worn-loot Player tombstone instead of a stock carcass (the
			// death hook gates on charStats+0x4A9==1). Mints a throwaway character
			// (CreateTestCharacter) and applies the #125 Strategy-P bind. Called
			// from Hooks::AnimalCreate::OnCreateAnimal for every bandit, so no manual
			// Lifx::bindLastAnimal is needed. Idempotent: re-binding a bound animal
			// is a no-op (returns false without minting a second character).
			bool BindHostile(void* animal);

			// #175 — record an item ObjectTypeID that we mounted on a bandit (e.g. the
			// held weapon). On death these are materialized as real `items` rows in the
			// grave so the bandit drops what it visibly carries. Called wherever we
			// mount an image (e.g. the held-weapon auto-mount). `bandit` is the SimObject
			// pointer the death hook also sees.
			void RecordMountedItem(void* bandit, uint32_t objectTypeId);

			// Re-apply setScopeAlways to every NPC ghosted via %npc.lifxGhost().
			// Driven by the setControlObject hook so a client that RECONNECTS
			// (without a server restart) re-scopes the NPCs instead of seeing
			// nothing. Safe to call often; prunes despawned NPCs.
			void ReScopeGhostNpcs();

			// #145 Step 2 — arm a cci-free worn-loot fill for a just-dead bandit
			// (charId from charStats+0x109C). The grave's loot container loads
			// asynchronously ~1s after death, so we don't fill on a timer; instead the
			// container-init hook calls OnGraveContainerCaptured the instant the grave
			// container loads (== the instant the tomb is openable), and the fill runs
			// then — guaranteeing loot is present on the first open. Called by the
			// death-redirect hook just before it fires the Player death trigger.
			// `bandit` is the dying SimObject — its recorded mounted items (#175) are
			// snapshotted now (while the object is alive) so they can be materialized
			// into the grave once its container is captured.
			void ArmGraveFill(uint32_t charId, void* bandit);

			// Invoked by the container-init hook when a grave (type-1070) container
			// loads. If a fill is armed, moves that bandit's items into this container
			// and reloads it, exactly once. graveContainerMid is the container's id.
			void OnGraveContainerCaptured(uint32_t graveContainerMid);

			// Deferred and immediate grave fills (manual / fallback use). The live path
			// is ArmGraveFill + OnGraveContainerCaptured above.
			void ScheduleBanditGraveFill(uint32_t charId);
			void FillBanditGraveNow(uint32_t charId, uint32_t graveContainerMid);
		}
	}
}
