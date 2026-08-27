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
	#145 — auto-tag Bandit animals at creation.

	Killability/movement need the engine's FULL spawn lifecycle (navmesh placement,
	collision, activation). Our raw createAnimal+setTransform doesn't integrate the
	animal, so it renders + faces but can't path or take weapon damage; an animal
	spawned the engine's own way (e.g. the GM `/animal BanditData` command, or a wild
	spawn) IS fully integrated and killable. So rather than re-implement placement, we
	let the engine spawn and hook the create chokepoint to mark our type as hostile.

	Hook on Animals::Manager::createAnimal (0x195FD0): after the engine builds an
	animal, if it's our Bandit type (animalTypeId 755) register it via
	Hooks::AnimRemap::Register so the setAnimation flood is suppressed and the
	death->tombstone redirect tracks it — regardless of HOW it was spawned.

	Signature: Animal* __fastcall createAnimal(mgr, u32 typeId, u32 quality, int id, u8 flag).
*  =================================================================================== */

#include "server/cm_server.h"
#include <cstdint>

__CM_DECL_EXTERNAL(void*, __fastcall, _Animal_CreateAnimal,
                   void* mgr, uint32_t typeId, uint32_t quality, int id, unsigned char flag);

namespace Hooks
{
	namespace AnimalCreate
	{
		void* __fastcall OnCreateAnimal(void* mgr, uint32_t typeId, uint32_t quality,
		                                int id, unsigned char flag);

		// The most recently created Bandit (type 755) animal, or nullptr. Lets a
		// console command operate on "the one I just /animal-spawned" without needing
		// its SimObjectId. Cleared best-effort; verify liveness before use.
		void* LastBandit();
	}
}
