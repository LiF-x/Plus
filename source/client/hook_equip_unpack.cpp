/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).
*  =================================================================================== */

#include "hook_equip_unpack.h"
#include "hook_console.h"          // _Engine_Con_InternalConsolePrintf (diag logging)
#include "hook_naked_render.h"     // _Engine_SetMeshHidden (resolved in AttachHooks)
#include "client/client_offsets.h"
#include "lifx_armor_meshes.h"     // kLifxArmorMeshes / kLifxArmorMeshCount
#include "lifx_equip_hide.h"       // kLifxEquipExtraHide (hair/beard cull for equipped NPCs)
#include "lifx_loadouts.h"         // kLifxLoadouts (per-type id -> mesh set)

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace
{
	// unpackUpdate is vtable slot 56 (offset 0x1C0). VERIFIED from the ghost-read
	// dispatch: `mov rax,[obj]; mov rdx,conn; mov r8,stream; call [rax+0x1C0]`,
	// target 0x2107A0 (inline bitstream read). NPCDecorative vtbl[56] = 0x46EF10
	// (jmp-thunk -> 0x2107A0); Player's is 0x2107A0 direct, so patching the NPC's
	// slot 56 is NPC-specific.
	constexpr unsigned kUnpackSlot = 56;

	// BitStream layout (verified both binaries).
	constexpr unsigned kBufOff      = 0x10;
	constexpr unsigned kBitPosOff   = 0x18;
	constexpr unsigned kCapBytesOff = 0x20;
	constexpr unsigned kOverflowOff = 0x28;
	// Shape-ready flag (same bit the naked-cull checks before setMeshHidden).
	constexpr unsigned kShapeReadyOff = 0x10;
	constexpr std::uint32_t kShapeReady = 0x400;

	using UnpackFn = void(__fastcall*)(void*, void*, void*);
	UnpackFn g_origUnpack  = nullptr;
	void**   g_patchedSlot = nullptr;
	unsigned long long g_calls = 0;

	// #171 — Animal vtable unpack patch (separate slot/orig from NPCDecorative).
	UnpackFn g_origAnimalUnpack  = nullptr;
	void**   g_animalPatchedSlot = nullptr;
	unsigned long long g_animalCalls = 0;

	// Last-applied loadout id per NPC object, so we only re-skin on change.
	std::unordered_map<void*, int> g_applied;

	inline int ReadBit(void* stream)
	{
		char* s = reinterpret_cast<char*>(stream);
		std::uint64_t bitPos  = *reinterpret_cast<std::uint64_t*>(s + kBitPosOff);
		std::uint64_t capBits = (*reinterpret_cast<std::uint64_t*>(s + kCapBytesOff)) * 8;
		if (bitPos + 1 > capBits) { *(s + kOverflowOff) = 1; return 0; }
		unsigned char* buf = *reinterpret_cast<unsigned char**>(s + kBufOff);
		int bit = (buf[bitPos >> 3] >> (bitPos & 7)) & 1;
		*reinterpret_cast<std::uint64_t*>(s + kBitPosOff) = bitPos + 1;
		return bit;
	}
	inline unsigned ReadBits(void* stream, int n)
	{
		unsigned v = 0;
		for (int i = 0; i < n; ++i) v |= (static_cast<unsigned>(ReadBit(stream)) << i);
		return v;
	}

	void diag(const char* fmt, ...)
	{
		if (!_Engine_Con_InternalConsolePrintf) return;
		char buf[256];
		va_list ap; va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		_Engine_Con_InternalConsolePrintf(0, 0, buf);
	}

	inline bool ShapeReady(void* self)
	{
		return (*reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(self) + kShapeReadyOff) & kShapeReady) != 0;
	}

	// Hide all armor/clothing/hair/underwear, then SHOW the loadout's meshes.
	void ApplyEquip(void* self, int loadoutId)
	{
		if (!_Engine_SetMeshHidden) return;
		for (unsigned i = 0; i < kLifxArmorMeshCount; ++i)
			_Engine_SetMeshHidden(self, kLifxArmorMeshes[i], 1);          // hide all armor/clothing
		for (unsigned i = 0; i < kLifxEquipExtraHideCount; ++i)
			_Engine_SetMeshHidden(self, kLifxEquipExtraHide[i], 1);       // hide every hair/beard/underwear variant
		if (loadoutId < 0 || (unsigned)loadoutId >= kLifxLoadoutCount)
			return;                                                       // unknown id -> naked (all hidden)
		const LifxLoadout& lo = kLifxLoadouts[loadoutId];
		if (lo.bareBody) {
			// Swap the default clothed body for bare skin so a thin chest piece
			// doesn't reveal the model's built-in chest tunic.
			for (unsigned i = 0; i < kDefaultBodyHideCount; ++i)
				_Engine_SetMeshHidden(self, kDefaultBodyHide[i], 1);      // hide Male_Body_ALL (clothed default)
			for (unsigned i = 0; i < kBareBodyShowCount; ++i)
				_Engine_SetMeshHidden(self, kBareBodyShow[i], 0);         // show bare skin parts
		}
		for (unsigned i = 0; i < lo.count; ++i)
			_Engine_SetMeshHidden(self, lo.meshes[i], 0);                 // show this loadout's gear
	}
}

void __fastcall LifxClient::HookEquipUnpack::OnUnpackUpdate(void* self, void* conn, void* stream)
{
	// Original unpack consumes the standard ghost fields; the server's appended
	// equip block follows immediately. Must read EXACTLY what the server wrote
	// (marker + loadoutId(8)) to keep the ghost packet framed.
	if (g_origUnpack) g_origUnpack(self, conn, stream);
	if (!self || !stream) return;

	const int marker = ReadBit(stream);
	const int loadoutId = (int)ReadBits(stream, 8);

	++g_calls;
	if (marker != 1) {            // framing sanity — should never happen
		if (g_calls <= 8) diag("LIFX-EQUIP: bad marker=%d (framing!)\n", marker);
		return;
	}

	// Re-skin only when the loadout changed for this object and its shape is ready.
	auto it = g_applied.find(self);
	const bool changed = (it == g_applied.end() || it->second != loadoutId);
	if (changed && ShapeReady(self)) {
		ApplyEquip(self, loadoutId);
		g_applied[self] = loadoutId;
		const char* nm = (loadoutId >= 0 && (unsigned)loadoutId < kLifxLoadoutCount)
		                 ? kLifxLoadouts[loadoutId].name : "<none>";
		diag("LIFX-EQUIP: applied loadout %d (%s) to NPC self=%p\n", loadoutId, nm, self);
	}
}

void __fastcall LifxClient::HookEquipUnpack::OnAnimalUnpackUpdate(void* self, void* conn, void* stream)
{
	// Mirror of the server's OnAnimalPackUpdate. The original unpack consumes the
	// standard ghost fields; then EVERY animal carries a 1-bit marker. Only when the
	// marker is set (our bandit) do the 8 loadout bits follow. A real animal's
	// marker is 0 -> read nothing more (NOT a framing error, unlike the NPCDec path).
	if (g_origAnimalUnpack) g_origAnimalUnpack(self, conn, stream);
	if (!self || !stream) return;

	const int marker = ReadBit(stream);
	if (marker != 1) return;                 // real wildlife — no loadout block
	const int loadoutId = (int)ReadBits(stream, 8);

	++g_animalCalls;
	auto it = g_applied.find(self);
	const bool changed = (it == g_applied.end() || it->second != loadoutId);
	if (changed && ShapeReady(self)) {
		ApplyEquip(self, loadoutId);
		g_applied[self] = loadoutId;
		const char* nm = (loadoutId >= 0 && (unsigned)loadoutId < kLifxLoadoutCount)
		                 ? kLifxLoadouts[loadoutId].name : "<none>";
		diag("LIFX-EQUIP: applied loadout %d (%s) to Animal bandit self=%p\n", loadoutId, nm, self);
	}
}

void LifxClient::HookEquipUnpack::InstallAnimalVtablePatch()
{
	char* base = reinterpret_cast<char*>(GetModuleHandle(NULL));
	void** vt  = reinterpret_cast<void**>(base + static_cast<unsigned>(ClientOffset::CLIENT_ANIMAL_VTABLE));
	void** slot = &vt[kUnpackSlot];

	void* cur = *slot;
	void* expectFn = base + static_cast<unsigned>(ClientOffset::CLIENT_ANIMAL_UNPACK_FN);
	diag("LIFX-EQUIP: Animal vtbl[%u] before patch = RVA 0x%llX (expect unpack 0x%X)\n",
	     kUnpackSlot, (unsigned long long)(reinterpret_cast<char*>(cur) - base),
	     (unsigned)ClientOffset::CLIENT_ANIMAL_UNPACK_FN);

	// Never patch an unverified slot: if it doesn't hold the RE'd unpackUpdate the
	// layout differs from ours — leaving it unpatched while the SERVER appends bits
	// would desync every animal ghost, so refuse and log loud.
	if (cur != expectFn) {
		diag("LIFX-EQUIP: Animal vtbl[%u] mismatch — NOT patching (bandit clothing off)\n", kUnpackSlot);
		return;
	}

	g_origAnimalUnpack = reinterpret_cast<UnpackFn>(cur);

	DWORD oldProt = 0;
	if (VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
		*slot = reinterpret_cast<void*>(&OnAnimalUnpackUpdate);
		VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
		g_animalPatchedSlot = slot;
		diag("LIFX-EQUIP: Animal vtbl[%u] patched -> OnAnimalUnpackUpdate (#171)\n", kUnpackSlot);
	} else {
		diag("LIFX-EQUIP: VirtualProtect FAILED — Animal vtable not patched\n");
		g_origAnimalUnpack = nullptr;
	}
}

void LifxClient::HookEquipUnpack::RemoveAnimalVtablePatch()
{
	if (!g_animalPatchedSlot || !g_origAnimalUnpack) return;
	DWORD oldProt = 0;
	if (VirtualProtect(g_animalPatchedSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
		*g_animalPatchedSlot = reinterpret_cast<void*>(g_origAnimalUnpack);
		VirtualProtect(g_animalPatchedSlot, sizeof(void*), oldProt, &oldProt);
	}
	g_animalPatchedSlot = nullptr;
}

void LifxClient::HookEquipUnpack::InstallVtablePatch()
{
	char* base = reinterpret_cast<char*>(GetModuleHandle(NULL));
	void** vt  = reinterpret_cast<void**>(base + static_cast<unsigned>(ClientOffset::CLIENT_NPCDEC_VTABLE));
	void** slot = &vt[kUnpackSlot];

	g_origUnpack = reinterpret_cast<UnpackFn>(*slot);
	diag("LIFX-EQUIP: NPCDec vtbl[%u] before patch = RVA 0x%llX (expect 0x46EF10 thunk)\n",
	     kUnpackSlot, (unsigned long long)(reinterpret_cast<char*>(*slot) - base));

	DWORD oldProt = 0;
	if (VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
		*slot = reinterpret_cast<void*>(&OnUnpackUpdate);
		VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
		g_patchedSlot = slot;
		diag("LIFX-EQUIP: NPCDec vtbl[%u] patched -> OnUnpackUpdate\n", kUnpackSlot);
	} else {
		diag("LIFX-EQUIP: VirtualProtect FAILED — vtable not patched\n");
	}
}

void LifxClient::HookEquipUnpack::RemoveVtablePatch()
{
	if (!g_patchedSlot || !g_origUnpack) return;
	DWORD oldProt = 0;
	if (VirtualProtect(g_patchedSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
		*g_patchedSlot = reinterpret_cast<void*>(g_origUnpack);
		VirtualProtect(g_patchedSlot, sizeof(void*), oldProt, &oldProt);
	}
	g_patchedSlot = nullptr;
}
