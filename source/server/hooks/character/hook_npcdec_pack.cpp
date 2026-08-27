/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_npcdec_pack.h"
#include "server/cm_offsets.h"
#include "server/hooks/character/hook_setanimation.h"   // AnimRemap::IsHostile (#171 bandit gate)

#include <Windows.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

__CM_INSTATNTIATE(_BitStream_WriteFlag);
__CM_INSTATNTIATE(_AiTree_Process);

namespace
{
	// Saved original (the NPC-only thunk 0x2E54C0 -> shared impl 0x0FC8B0).
	Hooks::NpcDecPack::PackFn g_origPack = nullptr;
	void** g_patchedSlot = nullptr;

	// #171 — Animal vtable pack patch (separate slot/orig from the NPCDecorative one).
	Hooks::NpcDecPack::PackFn g_origAnimalPack = nullptr;
	void** g_animalPatchedSlot = nullptr;
	std::atomic<unsigned long long> g_animalAppends{0};

	std::atomic<unsigned long long> g_npcAppends{0};

	// Per-NPC loadout id, assigned via %npc.lifxLoadout(id). The pack path reads
	// it every ghost update (frequent); the command writes it rarely. A mutex is
	// negligible at pack rate and keeps the map safe across threads.
	std::mutex g_loadoutMtx;
	std::unordered_map<void*, unsigned char> g_loadouts;

	// NPCs whose behaviour tree we tick each pack (NPCDecorative doesn't natively).
	std::mutex g_aiMtx;
	std::unordered_set<void*> g_aiNpcs;

	// Append `bits` low bits of `v`, LSB-first (matches the inlined readFlag).
	inline void AppendBits(void* stream, unsigned v, int bits)
	{
		for (int i = 0; i < bits; ++i)
			_BitStream_WriteFlag(stream, static_cast<char>((v >> i) & 1));
	}
}

unsigned long long __fastcall Hooks::NpcDecPack::OnPackUpdate(void* self, void* conn,
                                                             unsigned long long mask, void* stream)
{
	// Run the stock pack first; it leaves the stream positioned right after the
	// standard fields. We return its retMask unchanged.
	const unsigned long long ret = g_origPack ? g_origPack(self, conn, mask, stream) : 0;

	// Because this slot is NPCDecorative-only, every call here IS an NPC ghost —
	// no runtime vtable gate needed. Append the equip block.
	//   marker(1)=1, loadoutId(8)  -- the client owns id -> mesh-set tables
	//   (lifx_loadouts.h). Default id 0 (plate) until %npc.lifxLoadout(id) sets it.
	if (stream) {
		unsigned char loadoutId = 0;
		{
			std::lock_guard<std::mutex> lk(g_loadoutMtx);
			auto it = g_loadouts.find(self);
			if (it != g_loadouts.end()) loadoutId = it->second;
		}

		_BitStream_WriteFlag(stream, 1);          // marker
		AppendBits(stream, loadoutId, 8);         // loadout id

		const auto n = g_npcAppends.fetch_add(1, std::memory_order_relaxed) + 1;
		if (n <= 8 || (n % 256) == 0)
			Con::Echo("[lifx-equip] packed loadout %u on NPCDecorative pack #%llu (self=%p)",
			          (unsigned)loadoutId, (unsigned long long)n, self);
	}

	// AI tick: DISABLED. Calling AiTree::process(tree) bare here CRASHES — a tree
	// attached via setBehavior lacks the per-node AI-context/blackboard binding
	// that stock Animal::packUpdate sets up (helper calls around 0x454fa0/0x457xxx)
	// before it ticks. Re-enable only once we replicate that setup OR drive the
	// tick through the engine's full AI activation. g_aiNpcs kept for that work.
	(void)g_aiNpcs;
	return ret;
}

unsigned long long __fastcall Hooks::NpcDecPack::OnAnimalPackUpdate(void* self, void* conn,
                                                                   unsigned long long mask, void* stream)
{
	// Run the stock Animal pack first (its own packUpdate 0x18B450), then append the
	// equip block. This vtable is shared by ALL wildlife, so the FIRST bit is a
	// per-instance marker: 1 only for our hostiles, 0 for real animals. The client's
	// OnAnimalUnpackUpdate always reads this 1 bit (every animal) and reads the 8
	// loadout bits ONLY when it is set — so framing stays lockstep for every animal.
	const unsigned long long ret = g_origAnimalPack ? g_origAnimalPack(self, conn, mask, stream) : 0;

	if (stream) {
		const bool isBandit = Hooks::AnimRemap::IsHostile(self);
		_BitStream_WriteFlag(stream, isBandit ? 1 : 0);      // marker (read for every animal)
		if (isBandit) {
			unsigned char loadoutId = 0;                     // default 0 = plate (#171)
			{
				std::lock_guard<std::mutex> lk(g_loadoutMtx);
				auto it = g_loadouts.find(self);
				if (it != g_loadouts.end()) loadoutId = it->second;
			}
			AppendBits(stream, loadoutId, 8);

			const auto n = g_animalAppends.fetch_add(1, std::memory_order_relaxed) + 1;
			if (n <= 8 || (n % 256) == 0)
				Con::Echo("[lifx-equip] packed loadout %u on Animal bandit pack #%llu (self=%p)",
				          (unsigned)loadoutId, (unsigned long long)n, self);
		}
	}
	return ret;
}

void Hooks::NpcDecPack::InstallAnimalVtablePatch()
{
	char* base = reinterpret_cast<char*>(GetModuleHandle(nullptr));
	void** vt  = reinterpret_cast<void**>(base + static_cast<unsigned>(CmOffset::ANIMAL_VTABLE));
	void** slot = &vt[static_cast<unsigned>(CmOffset::ANIMAL_PACKUPDATE_SLOT)];

	void* cur = *slot;
	void* expectFn = base + static_cast<unsigned>(CmOffset::ANIMAL_PACKUPDATE_FN);

	// Same hard rule as the NPCDecorative install: never patch an unverified slot.
	if (cur != expectFn) {
		Con::Warning("[lifx-equip] Animal vtbl[%u] = +0x%llX, expected packUpdate +0x%X — NOT patching (layout mismatch)",
		             (unsigned)CmOffset::ANIMAL_PACKUPDATE_SLOT,
		             (unsigned long long)(reinterpret_cast<char*>(cur) - base),
		             (unsigned)CmOffset::ANIMAL_PACKUPDATE_FN);
		return;
	}

	g_origAnimalPack = reinterpret_cast<PackFn>(cur);

	DWORD oldProt = 0;
	if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
		*slot = reinterpret_cast<void*>(&OnAnimalPackUpdate);
		VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
		g_animalPatchedSlot = slot;
		Con::Echo("[lifx-equip] Animal vtbl[%u] patched -> OnAnimalPackUpdate (orig packUpdate +0x%X). Bandit clothing render armed. #171",
		          (unsigned)CmOffset::ANIMAL_PACKUPDATE_SLOT, (unsigned)CmOffset::ANIMAL_PACKUPDATE_FN);
	} else {
		Con::Warning("[lifx-equip] VirtualProtect FAILED on Animal vtbl slot — bandit clothing disabled");
		g_origAnimalPack = nullptr;
	}
}

void Hooks::NpcDecPack::RemoveAnimalVtablePatch()
{
	if (!g_animalPatchedSlot || !g_origAnimalPack) return;
	DWORD oldProt = 0;
	if (VirtualProtect(g_animalPatchedSlot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
		*g_animalPatchedSlot = reinterpret_cast<void*>(g_origAnimalPack);
		VirtualProtect(g_animalPatchedSlot, sizeof(void*), oldProt, &oldProt);
	}
	g_animalPatchedSlot = nullptr;
}

void Hooks::NpcDecPack::InstallVtablePatch()
{
	char* base = reinterpret_cast<char*>(GetModuleHandle(nullptr));
	void** vt  = reinterpret_cast<void**>(base + static_cast<unsigned>(CmOffset::NPCDEC_VTABLE));
	void** slot = &vt[static_cast<unsigned>(CmOffset::NPCDEC_PACKUPDATE_SLOT)];

	void* cur = *slot;
	void* expectThunk = base + static_cast<unsigned>(CmOffset::NPCDEC_PACKUPDATE_THUNK);

	// Hard rule: never hand the engine an unverified pointer. If the slot doesn't
	// hold the documented thunk, the layout differs from our RE — bail loudly and
	// leave equipment rendering off rather than risk a wrong call.
	if (cur != expectThunk) {
		Con::Warning("[lifx-equip] NPCDecorative vtbl[%u] = +0x%llX, expected thunk +0x%X — NOT patching (layout mismatch)",
		             (unsigned)CmOffset::NPCDEC_PACKUPDATE_SLOT,
		             (unsigned long long)(reinterpret_cast<char*>(cur) - base),
		             (unsigned)CmOffset::NPCDEC_PACKUPDATE_THUNK);
		return;
	}

	g_origPack = reinterpret_cast<PackFn>(cur);

	DWORD oldProt = 0;
	if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
		*slot = reinterpret_cast<void*>(&OnPackUpdate);
		VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
		g_patchedSlot = slot;
		Con::Echo("[lifx-equip] NPCDecorative vtbl[%u] patched -> OnPackUpdate (orig thunk +0x%X). NO Detours on the shared pack path.",
		          (unsigned)CmOffset::NPCDEC_PACKUPDATE_SLOT, (unsigned)CmOffset::NPCDEC_PACKUPDATE_THUNK);
	} else {
		Con::Warning("[lifx-equip] VirtualProtect FAILED on NPCDecorative vtbl slot — equipment render disabled");
		g_origPack = nullptr;
	}
}

void Hooks::NpcDecPack::SetLoadout(void* npc, unsigned char loadoutId)
{
	if (!npc) return;
	std::lock_guard<std::mutex> lk(g_loadoutMtx);
	g_loadouts[npc] = loadoutId;
}

void Hooks::NpcDecPack::SetAiTick(void* npc, bool on)
{
	if (!npc) return;
	std::lock_guard<std::mutex> lk(g_aiMtx);
	if (on) g_aiNpcs.insert(npc);
	else    g_aiNpcs.erase(npc);
}

void Hooks::NpcDecPack::RemoveVtablePatch()
{
	if (!g_patchedSlot || !g_origPack) return;
	DWORD oldProt = 0;
	if (VirtualProtect(g_patchedSlot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
		*g_patchedSlot = reinterpret_cast<void*>(g_origPack);
		VirtualProtect(g_patchedSlot, sizeof(void*), oldProt, &oldProt);
	}
	g_patchedSlot = nullptr;
}
