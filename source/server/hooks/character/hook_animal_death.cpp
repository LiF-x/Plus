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

#include "hook_animal_death.h"
#include "server/cm_offsets.h"
#include "server/hooks/character/hook_setanimation.h"   // AnimRemap::IsHostile (our-animal gate)
#include "server/hooks/furnace/engine_internals.h"      // Engine::ModuleBase
#include "server/api/lifx_hostile.h"                     // ArmGraveFill (#145 Step 2)

#include <cstdint>
#include <windows.h>

__CM_INSTATNTIATE(_Animal_CreateCorpse);

namespace
{
	// Synchronous death handling is single-threaded; a thread-local guard prevents
	// recursion if the Player death trigger routes back through the death router and
	// re-enters createCorpse for the same animal.
	thread_local bool tlInRedirect = false;

	bool SafeReadable(const void* p, size_t len)
	{
		if (!p) return false;
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
		if (mbi.State != MEM_COMMIT) return false;
		const DWORD prot = mbi.Protect;
		const bool readable = (prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
		                               PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) != 0;
		if (!readable || (prot & PAGE_NOACCESS) || (prot & PAGE_GUARD)) return false;
		return reinterpret_cast<const char*>(p) + len > reinterpret_cast<const char*>(p);
	}

	bool InModule(const void* p)
	{
		const uintptr_t base = ::Engine::ModuleBase();
		const uintptr_t v = reinterpret_cast<uintptr_t>(p);
		return v >= base && v < base + 0x01000000;
	}
}

void __fastcall Hooks::AnimalDeath::OnCreateCorpse(void* animal)
{
	// Non-hostile (real wild animal) -> stock carcass, untouched.
	if (!animal || !Hooks::AnimRemap::IsHostile(animal)) { _Animal_CreateCorpse(animal); return; }

	// Re-entry (death trigger routed back here) -> suppress: no carcass, no recursion.
	if (tlInRedirect) return;

	// #154: cancel any queued contact-frame hit + clear combat state before this bandit's
	// memory is reclaimed, so OnHitTick never fires endAttack on a freed animal.
	Hooks::AnimRemap::Forget(animal);

	const uintptr_t base = ::Engine::ModuleBase();
	void* charStats = static_cast<char*>(animal) + ::Engine::kCharStatsToPlayerDelta;   // +0xAA8
	if (!SafeReadable(charStats, sizeof(void*))) { _Animal_CreateCorpse(animal); return; }

	// Only redirect to the Player tombstone if this animal is CHARACTER-BOUND
	// (charStats+0x4A9 == 1, set by the #125-style bind). Without a bound character
	// the Player death path has no char info -> the lootstone DB insert fails the
	// OwnerID foreign key and rolls back (lost loot). Until the bind is wired in,
	// fall through to the normal carcass so loot is never lost. #145.
	if (!SafeReadable(static_cast<char*>(charStats) + 0x4A9, 1) ||
	    *(static_cast<uint8_t*>(charStats) + 0x4A9) != 1) {
		_Animal_CreateCorpse(animal);
		return;
	}

	void** csVt = *reinterpret_cast<void***>(charStats);
	const unsigned slot = static_cast<unsigned>(CmOffset::CHARSTATS_DEATH_TRIGGER_SLOT) / 8;  // 0x130 -> idx 38
	// NOTE: do NOT require csVt to be in-module — once char-bound, the charStats vtable
	// is our CLONED vtable on the heap (valid, readable). Only the death-trigger
	// FUNCTION it points to must be in-module (checked below).
	if (!SafeReadable(csVt, (slot + 1) * sizeof(void*))) {
		Con::Warning("[lifx-death] obj=%p charStats vtable unreadable; falling back to carcass", animal);
		_Animal_CreateCorpse(animal);
		return;
	}
	void* fn = csVt[slot];
	if (!InModule(fn)) {
		Con::Warning("[lifx-death] obj=%p charStats vtbl[0x130]=%p not in module; carcass fallback "
		             "(animal charStats class may differ from Player's)", animal, fn);
		_Animal_CreateCorpse(animal);
		return;
	}

	// The bound charId lives at charStats+0x109C (written by the #125-style bind).
	// Grab it BEFORE firing the trigger (the trigger won't clear it, but read it
	// while we know charStats is valid) so we can fill the grave afterwards. #145.
	constexpr unsigned kCharIdFieldOff = 0x109C;
	uint32_t boundCharId = 0;
	if (SafeReadable(static_cast<char*>(charStats) + kCharIdFieldOff, 4))
		boundCharId = *reinterpret_cast<uint32_t*>(static_cast<char*>(charStats) + kCharIdFieldOff);

	// #145 Step 2: the trigger creates the grave (movable_objects, type 1070, valid
	// OwnerID) but the item TRANSFER into it is cci-gated and skipped for our
	// connection-less bandit -> empty tombstone. The grave's loot container loads
	// ASYNChronously (~1s after death), so we ARM the fill here and let the
	// container-init hook execute it the instant that container is captured — which
	// is also the instant the tomb becomes openable, so loot is present on first open.
	// Arm BEFORE firing the trigger so even a synchronous capture is covered.
	if (boundCharId != 0)
		Lifx::Api::Hostile::ArmGraveFill(boundCharId, animal);   // #175 snapshots mounted gear

	// Invoke the SAME trigger the engine's death router uses for a dying Player:
	// charStats->vtbl[0x130](charStats, 0)  -> lootstone + worn-loot pipeline.
	using PfnDeath = void(__fastcall*)(void* charStats, long long zero);
	tlInRedirect = true;
	reinterpret_cast<PfnDeath>(fn)(charStats, 0);
	tlInRedirect = false;

	// createCorpse's tail: tell the Animals::Manager to forget this animal, so the
	// spawn manager doesn't think it's still alive.
	void* mgr = *reinterpret_cast<void**>(base + static_cast<unsigned>(CmOffset::ANIMALS_MANAGER_GLOBAL_RVA));
	if (mgr) {
		const uint32_t mgrId = *reinterpret_cast<uint32_t*>(
			static_cast<char*>(animal) + static_cast<unsigned>(CmOffset::ANIMAL_MGR_ID_OFF));
		using PfnRemove = void(__fastcall*)(void* mgr, uint32_t id);
		reinterpret_cast<PfnRemove>(base + static_cast<unsigned>(CmOffset::ANIMAL_MGR_REMOVE))(mgr, mgrId);
	}

	Con::Echo("[lifx-death] obj=%p: carcass suppressed -> Player death trigger (charStats vtbl[0x130]) "
	          "fired; expect a lootstone tombstone (worn loot once equipped). #145", animal);
}
