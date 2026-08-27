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

#include "hook_animal_create.h"
#include "server/hooks/character/hook_setanimation.h"   // AnimRemap::Register
#include "server/api/lifx_hostile.h"                    // Hostile::BindHostile (#169 auto-bind)

#include <atomic>

__CM_INSTATNTIATE(_Animal_CreateAnimal);

namespace
{
	// Bandit shares the Wolf animal type id (755) — the type->datablock map returns
	// BanditData (male.dts) for 755 on this build. A dedicated Bandit type id is the
	// (separate) type-registration task; until then, 755 IS our hostile.
	constexpr uint32_t kBanditTypeId = 755;

	std::atomic<void*> g_lastBandit{nullptr};
}

void* Hooks::AnimalCreate::LastBandit()
{
	return g_lastBandit.load(std::memory_order_relaxed);
}

void* __fastcall Hooks::AnimalCreate::OnCreateAnimal(void* mgr, uint32_t typeId, uint32_t quality,
                                                     int id, unsigned char flag)
{
	void* animal = _Animal_CreateAnimal(mgr, typeId, quality, id, flag);
	if (animal && typeId == kBanditTypeId) {
		// Tag every Bandit animal (however spawned) so the anim remap applies and the
		// death->tombstone redirect tracks it; remember the latest for bindLastAnimal.
		Hooks::AnimRemap::Register(animal);
		g_lastBandit.store(animal, std::memory_order_relaxed);
		// #169 — character-bind it NOW so its death drops a worn-loot tombstone
		// without a manual Lifx::bindLastAnimal. The death hook gates on the bind
		// (charStats+0x4A9==1); autonomously/wild-spawned bandits otherwise die as
		// stock carcasses. BindHostile is idempotent and fully guarded.
		Lifx::Api::Hostile::BindHostile(animal);
	}
	return animal;
}
