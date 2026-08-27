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
	Engine-internal function pointers + offset constants used by the LiFx
	reimplementations of craftwork tick functions.

	These are *not* hook targets — they're the engine functions our
	reimplementations CALL into to do the same work the engine does. Each
	pointer is lazy-resolved by RVA on first use; thread-safe via C++11
	function-local statics.

	Naming convention (matches docs/conventions.md):
	  - Free helpers in the engine get a flat name reflecting their semantics
	    (ServerTime_Now, Type_HasParent, etc.).
	  - The RVA literal in each accessor's static initializer is the single
	    source of truth — if a future LiF patch shifts offsets, all uses
	    update by editing one line per function.

	Add new entries here as you reimplement more craftwork ticks. Group by
	subsystem; alphabetize within a group.
*/

#include "core/cm_aux.h"

namespace Engine
{
	// ---- module base -----------------------------------------------------
	inline uintptr_t ModuleBase() {
		static const uintptr_t base = (uintptr_t)GetModuleHandle(nullptr);
		return base;
	}

	template <typename Fn>
	inline Fn AtRva(uintptr_t rva) {
		return reinterpret_cast<Fn>(ModuleBase() + rva);
	}

	template <typename T>
	inline T* DataAtRva(uintptr_t rva) {
		return reinterpret_cast<T*>(ModuleBase() + rva);
	}

	// ============================================================================
	// SERVER TIME
	// ============================================================================
	// Server-wide "now" timestamp in engine ticks. Used by every craftwork
	// recalcTick as the "current time" reference.
	typedef unsigned (__fastcall *pfn_serverTime_now)();
	inline unsigned ServerTime_Now() {
		static const auto fn = AtRva<pfn_serverTime_now>(0x5147A0);
		return fn();
	}

	// ============================================================================
	// TYPE SYSTEM
	// ============================================================================
	// DAT_140B53908 = pointer to the global ObjectTypeManager singleton. The
	// hasParent / getTypeById helpers below take an offset of (+8) into this
	// manager to reach the actual type table. The +8 is convention from the
	// engine; we keep it here to match the decompile literally.
	inline void* TypeManagerWithChildOffset() {
		return *DataAtRva<void*>(0xB53908) ? reinterpret_cast<char*>(*DataAtRva<void*>(0xB53908)) + 8 : nullptr;
	}

	// Returns 1 if `typeInfo` is a descendant of `targetTypeId` within `maxDepth`
	// ParentID hops; otherwise 0. Hook target lives at 0x27EB30 — but we're
	// CALLING the engine's implementation here, not the hooked version.
	typedef int (__fastcall *pfn_type_hasParent)(void* typeInfo, int targetTypeId, int maxDepth);
	inline int Type_HasParent(void* typeInfo, int targetTypeId, int maxDepth = 100) {
		static const auto fn = AtRva<pfn_type_hasParent>(0x27EB30);
		return fn(typeInfo, targetTypeId, maxDepth);
	}

	// Resolve a typeId to its Type* via the global manager. NULL if unknown.
	typedef void* (__fastcall *pfn_type_getById)(void* mgr, int typeId);
	inline void* Type_GetById(int typeId) {
		static const auto fn = AtRva<pfn_type_getById>(0x27CA00);
		return fn(TypeManagerWithChildOffset(), typeId);
	}

	// ============================================================================
	// INVENTORY ITERATOR (shared across all craftwork recalcTicks)
	// ============================================================================
	// The engine constructs a 72-byte iterator on the stack, walks the contents
	// of a container, and uses the *_Inv_* helpers below to mutate slot state.
	// In the decompiles these appear as FUN_14029B750, FUN_14029C8A0, etc.
	typedef void  (__fastcall *pfn_inv_begin)(void* iterator, void* containerCtx);
	typedef char  (__fastcall *pfn_inv_valid)(void* iterator);
	typedef void  (__fastcall *pfn_inv_finish)(void* iterator);
	typedef void  (__fastcall *pfn_inv_teardown)(void* iterator);
	typedef void  (__fastcall *pfn_inv_advance_progress)(void* iterator, void* slot, unsigned amount);
	typedef void  (__fastcall *pfn_inv_advance_alt)(void* iterator, void* slot, unsigned amount);
	typedef void  (__fastcall *pfn_inv_set_quality)(void* iterator, void* slot, unsigned q);
	typedef void  (__fastcall *pfn_inv_abort)(void* iterator, void* slot);

	inline void Inv_Begin(void* iterator, void* containerCtx) {
		static const auto fn = AtRva<pfn_inv_begin>(0x29B750);
		fn(iterator, containerCtx);
	}
	inline char Inv_Valid(void* iterator) {
		static const auto fn = AtRva<pfn_inv_valid>(0x29C8A0);
		return fn(iterator);
	}
	inline void Inv_Finish(void* iterator) {
		static const auto fn = AtRva<pfn_inv_finish>(0x29C600);
		fn(iterator);
	}
	inline void Inv_Teardown(void* iterator) {
		static const auto fn = AtRva<pfn_inv_teardown>(0x29BBE0);
		fn(iterator);
	}
	inline void Inv_AdvanceProgress(void* iterator, void* slot, unsigned amount) {
		static const auto fn = AtRva<pfn_inv_advance_progress>(0x29D950);
		fn(iterator, slot, amount);
	}
	inline void Inv_AdvanceAlt(void* iterator, void* slot, unsigned amount) {
		static const auto fn = AtRva<pfn_inv_advance_alt>(0x29DBD0);
		fn(iterator, slot, amount);
	}
	inline void Inv_SetQuality(void* iterator, void* slot, unsigned q) {
		static const auto fn = AtRva<pfn_inv_set_quality>(0x29DD30);
		fn(iterator, slot, q);
	}
	inline void Inv_Abort(void* iterator, void* slot) {
		static const auto fn = AtRva<pfn_inv_abort>(0x29DAA0);
		fn(iterator, slot);
	}

	// ============================================================================
	// FURNACE / BREWING HELPERS
	// ============================================================================
	typedef int (__fastcall *pfn_furnace_get_temp)(void* self);
	typedef void (__fastcall *pfn_furnace_set_temp)(void* self, int t);
	inline int Furnace_GetTemperature(void* self) {
		static const auto fn = AtRva<pfn_furnace_get_temp>(0x1D5D40);
		return fn(self);
	}
	inline void Furnace_SetTemperature(void* self, int t) {
		static const auto fn = AtRva<pfn_furnace_set_temp>(0x1D5D50);
		fn(self, t);
	}

	// Fetches the container view associated with the furnace (param_1+0x0C).
	typedef void (__fastcall *pfn_furnace_get_contents)(void* self, void* outContainer);
	inline void Furnace_GetContents(void* self, void* outContainer) {
		static const auto fn = AtRva<pfn_furnace_get_contents>(0x1DF4B0);
		fn(self, outContainer);
	}

	// Visitor — invokes the supplied callback for each... something, per furnace.
	// Used by Greenhouse for the plant-growth walk.
	typedef void (__fastcall *pfn_furnace_visit)(void* self, void* callbackObj);
	inline void Furnace_Visit(void* self, void* callbackObj) {
		static const auto fn = AtRva<pfn_furnace_visit>(0x1DF4F0);
		fn(self, callbackObj);
	}

	// Windmill helper called by WorkingWindmill::recalcTick. Takes no args and
	// touches global state (likely "advance the grindstone counter"). Engine
	// label unknown; we keep the FUN_-style name to be honest.
	typedef void (*pfn_windmill_helper)();
	inline void Windmill_TickHelper() {
		static const auto fn = AtRva<pfn_windmill_helper>(0x1DFB20);
		fn();
	}

	// ============================================================================
	// DESCRIPTOR LOOKUPS (hook targets — calling the originals via RVA gives
	//                    us the engine's unmodified table walk)
	// ============================================================================
	// NB: these duplicate the LiFx trampolines used in hook_proc_desc.cpp /
	// hook_brewing_tank_desc.cpp, but going through the trampolines would
	// re-enter our own hooks. From a reimplementation we want the engine's
	// original lookup behavior, not the hooked one — so we call by RVA.
	typedef void* (__fastcall *pfn_desc_lookup)(void* itemTypeInfo);
	inline void* Furnace_OriginalLookup(void* itemTypeInfo) {
		static const auto fn = AtRva<pfn_desc_lookup>(0x1DB7C0);
		return fn(itemTypeInfo);
	}
	inline void* BrewingTank_OriginalLookup(void* itemTypeInfo) {
		static const auto fn = AtRva<pfn_desc_lookup>(0x1DAAE0);
		return fn(itemTypeInfo);
	}

	// Alternative brewing lookup keyed by a different row field (+0x18 vs +0x00).
	inline void* BrewingTank_OriginalLookupAlt(void* itemTypeInfo) {
		static const auto fn = AtRva<pfn_desc_lookup>(0x1DAB60);
		return fn(itemTypeInfo);
	}

	// ============================================================================
	// CONSTANTS (read from .rdata)
	// ============================================================================
	inline float MaxTickDt()     { return *DataAtRva<float>(0x737008); }   // 1.0f clamp
	inline float TickRateUnits() { return *DataAtRva<float>(0x73AB78); }   // dt -> "progress units" multiplier

	// ============================================================================
	// CHARACTER LOOKUPS / STATE
	// ============================================================================
	// CmServer global singleton — holds object types, character info, and many
	// other manager-shaped collections. DAT_140B53908 is its instance pointer.
	inline void* CmServer() {
		return *DataAtRva<void*>(0xB53908);
	}

	// Look up CmCharacterInfo* by character ID. The engine signature is
	//     FUN_14028BC20(mgr, out**, charId, flag);
	// where `out` receives the CmCharacterInfo* or NULL if not found.
	typedef void (__fastcall *pfn_get_character_info)(void* mgr, void** out,
	                                                   uint32_t charId, int flag);
	inline void* Character_GetByID(uint32_t charId) {
		static const auto fn = AtRva<pfn_get_character_info>(0x28BC20);
		void* out = nullptr;
		fn(CmServer(), &out, charId, 0);
		return out;
	}

	// Persist a CmCharacterInfo's HardHP and SoftHP to the DB. Engine fn at
	// 0x1BB290 — synchronously enqueues the UPDATE.
	typedef void (__fastcall *pfn_persist_hp)(void* charInfo);
	inline void Character_PersistHp(void* charInfo) {
		static const auto fn = AtRva<pfn_persist_hp>(0x1BB290);
		fn(charInfo);
	}

	// Mark a CmCharacterInfo as dirty (via bitmask of changed fields) and
	// optionally broadcast a state-update event to the owning client.
	//
	//   mask     — bitmask of which fields changed (1<<statType for primary
	//              stats; HP uses higher bits whose exact assignment we
	//              haven't enumerated, so passing 0xFFFFFFFF forces a full
	//              resync which is wasteful but reliable).
	//   sendNow  — 0 to just OR the bitmask into the dirty field (deferred
	//              broadcast on the next aggregated send), 1 to broadcast
	//              immediately.
	//
	// Engine fn at 0x1BC3D0 (CmCharacterInfo::_sendChanges).
	typedef void (__fastcall *pfn_send_changes)(void* charInfo, unsigned mask, char sendNow);
	inline void Character_SendChanges(void* charInfo, unsigned mask, char sendNow) {
		static const auto fn = AtRva<pfn_send_changes>(0x1BC3D0);
		fn(charInfo, mask, sendNow);
	}

	// HP is stored as a scaled int32 at +0x194: internal_value = display * 1e6.
	// Confirmed against DAT_140737AB80 (the engine's scale-double constant).
	constexpr int kHpScale = 1000000;

	// On the Player's character-stats sub-object (= charStats, hooked as
	// Process_tick.self / Calc_hit_damage.self / HitApplyDamage.charStats):
	//
	//     Player + 0xAA8  = charStats   (so charStats - 0xAA8 = Player)
	//     Player + 0x1B44 = charID (uint32, derived from Player::_applyHit
	//                              decompile lines 597, 716, 717)
	//
	// Therefore from any charStats pointer:
	//     charID = *(uint32*)(charStats + 0x109C)
	//
	// where 0x109C = 0x1B44 - 0xAA8. Verified empirically — this is what the
	// charID→charStats registry in hook_vital_process_tick uses.
	constexpr unsigned kCharStatsToPlayerDelta = 0xAA8;
	constexpr unsigned kCharIdOffOnCharStats   = 0x109C;

	// HP is a TRIPLET, not a single field. From the process_tick decomp at
	// RVA 0x97BC0 (death-gate at line 313):
	//
	//     if ((param_1[0x27] - param_1[0x23]) + param_1[0x24] < 1)
	//         vtable[0xa8](self);   // death virtual
	//
	// i.e. hard HP effective = (+0x138 - +0x118 + +0x120). The HUD reads
	// effective, not +0x118 alone. Soft HP is the parallel triplet
	// (+0x218 - +0x1F8 + +0x200) — see kSoft*FieldOff below.
	//
	// +0x118 happens to drift opposite the HUD for hard HP, which is why
	// the "HUD = floor(-field/1e6) + 1" shortcut worked there empirically.
	// That shortcut is brittle — it falls apart when wound state moves
	// +0x138 or +0x120. Long-term we should compute HUD from the triplet.
	constexpr unsigned kHardHpDamageOff   = 0x118;   // damage term (subtracted)
	constexpr unsigned kHardHpBonusOff    = 0x120;   // bonus term (added)
	constexpr unsigned kHardHpEffMaxOff   = 0x138;   // effective max term
	constexpr unsigned kHardHpFieldOff    = 0x118;   // legacy alias for the damage term
	constexpr unsigned kHardHpMaxFieldOff = 0x110;   // nominal max HP (Constitution cap)
	inline long long HardHpRaw(void* charStats) {
		return *reinterpret_cast<long long*>(static_cast<char*>(charStats) + kHardHpFieldOff);
	}
	inline long long HardHpMaxRaw(void* charStats) {
		return *reinterpret_cast<long long*>(static_cast<char*>(charStats) + kHardHpMaxFieldOff);
	}
	// Empirical: HUD hard HP = floor(-field/1e6) + 1 (1-based "minimum
	// alive = 1" display), capped at max_display.
	inline long long HardHpDisplay(void* charStats) {
		const long long internal   = -HardHpRaw(charStats) / kHpScale;
		const long long maxRaw     = HardHpMaxRaw(charStats);
		const long long maxDisplay = (maxRaw > 0) ? (maxRaw / kHpScale) : 105;
		const long long hud        = internal + 1;
		return hud > maxDisplay ? maxDisplay : hud;
	}

	// Soft HP triplet (process_tick line ~100):
	//     soft_effective = (+0x218 - +0x1F8 + +0x200)
	// Same shape as the hard triplet. Direct writes to +0x1F8 don't
	// budge the HUD because effective is a sum of three terms — writing
	// one to a "negated HP" value just makes the subtracted term large
	// and effective gets clamped at max. To MOVE the HUD you have to
	// either preserve the triplet invariant or update +0x218 / +0x200.
	constexpr unsigned kSoftHpDamageOff   = 0x1F8;   // damage term (subtracted)
	constexpr unsigned kSoftHpBonusOff    = 0x200;   // bonus term (added)
	constexpr unsigned kSoftHpEffMaxOff   = 0x218;   // effective max term
	constexpr unsigned kSoftHpFieldOff    = 0x1F8;   // legacy alias
	constexpr unsigned kSoftHpMaxFieldOff = 0x1F0;   // nominal max soft HP
	inline long long SoftHpRaw(void* charStats) {
		return *reinterpret_cast<long long*>(static_cast<char*>(charStats) + kSoftHpFieldOff);
	}
	inline long long SoftHpMaxRaw(void* charStats) {
		return *reinterpret_cast<long long*>(static_cast<char*>(charStats) + kSoftHpMaxFieldOff);
	}
	// Empirical: HUD soft HP = floor(-field/1e6) (no offset), capped at max.
	inline long long SoftHpDisplay(void* charStats) {
		const long long internal   = -SoftHpRaw(charStats) / kHpScale;
		const long long maxRaw     = SoftHpMaxRaw(charStats);
		const long long maxDisplay = (maxRaw > 0) ? (maxRaw / kHpScale) : 105;
		return internal > maxDisplay ? maxDisplay : internal;
	}

	// Triplet field accessors — read the three terms the engine uses
	// internally for the effective-HP / death-check calculation.
	inline long long ReadI64At(void* base, unsigned off) {
		return *reinterpret_cast<long long*>(static_cast<char*>(base) + off);
	}
	inline long long HardHpEffective(void* cs) {
		return ReadI64At(cs, kHardHpEffMaxOff)
		     - ReadI64At(cs, kHardHpDamageOff)
		     + ReadI64At(cs, kHardHpBonusOff);
	}
	inline long long SoftHpEffective(void* cs) {
		return ReadI64At(cs, kSoftHpEffMaxOff)
		     - ReadI64At(cs, kSoftHpDamageOff)
		     + ReadI64At(cs, kSoftHpBonusOff);
	}

	// CmCharacterWounds::dealDamage(this, int bodyPart) — applies staged
	// damage to all 3 injury types of the given body part (0..5). Engine RVA
	// 0x1C63E0. We use this directly from a LiFx command after the hook on
	// it captures a live CmCharacterWounds* for a player.
	typedef void (__fastcall *pfn_wounds_deal_damage)(void* self, int bodyPart);
	inline void Wounds_DealDamage(void* self, int bodyPart) {
		static const auto fn = AtRva<pfn_wounds_deal_damage>(0x1C63E0);
		fn(self, bodyPart);
	}

	// ============================================================================
	// CONSOLE LOGGING (rare — most hooks should use Con::Echo via t3d_console.h)
	// ============================================================================
	typedef char (__fastcall *pfn_console_enabled)();
	typedef void (__fastcall *pfn_console_printf)(unsigned type, unsigned unk, const char* msg);
	inline char Console_Enabled() {
		static const auto fn = AtRva<pfn_console_enabled>(0x405040);
		return fn();
	}
	inline void Console_Printf(unsigned type, const char* msg) {
		static const auto fn = AtRva<pfn_console_printf>(0x405090);
		fn(type, 0, msg);
	}

	// ============================================================================
	// STRUCT OFFSETS (used by reimplementations to read engine state)
	// ============================================================================
	// Each constant names an offset within an engine object. Read like:
	//     int temp = *(int*)((char*)workingFurnaceSelf + ::Engine::Off::Furnace_State);
	// Confirmed against the decompiles in /tmp/lifx_ghidra/decompile/rt_*.c.
	namespace Off
	{
		// WorkingFire / WorkingFurnace / BrewingTankFurnace — shared base layout
		constexpr unsigned Windmill_LastTick   = 0x08;   // Windmill: timestamp of last tick
		constexpr unsigned Furnace_ContentsRef = 0x0C;   // pointer that Furnace_GetContents reads
		constexpr unsigned Furnace_StateField  = 0x14;   // [5]: furnace state (2000=hot, 500=cool)
		constexpr unsigned Furnace_KindlingBurn= 0x18;   // [6]: kindling-burn counter
		constexpr unsigned Furnace_StateAlt    = 0x28;   // alternate state field used by Fire/Greenhouse
		constexpr unsigned Furnace_Temperature = 0x2C;   // current temperature accumulator
		constexpr unsigned Furnace_FreezeFlag  = 0x34;   // "skip temperature decay" flag

		// Inventory slot layout
		constexpr unsigned Slot_ItemData       = 0x18;   // pointer to the per-instance item data
		constexpr unsigned Slot_QualityField   = 0x30;   // current quality (0..100)

		// ItemData layout
		constexpr unsigned ItemData_TypeId     = 0x08;   // uint32 type id

		// CmCharacterInfo layout (relevant fields)
		constexpr unsigned CharInfo_HardHP     = 0x194;  // current HP (int32)
		constexpr unsigned CharInfo_SoftHP     = 0x19C;  // HP soft cap   (int32)
		constexpr unsigned CharInfo_StatsCount = 0x308;  // int32 — entries in name-table
		constexpr unsigned CharInfo_StatsArray = 0x310;  // ptr to stat array (24-byte rows)
		constexpr unsigned CharInfo_InitFlag   = 0x358;  // 0 if not initialized

		// Process-descriptor row (28 bytes total)
		constexpr unsigned ProcDesc_TypeId         = 0x00;
		constexpr unsigned ProcDesc_Kind           = 0x04;
		constexpr unsigned ProcDesc_Factor         = 0x08;
		constexpr unsigned ProcDesc_OutputType     = 0x0C;
		constexpr unsigned ProcDesc_Flag           = 0x10;
		constexpr unsigned ProcDesc_Field5         = 0x14;
		constexpr unsigned ProcDesc_TempThreshold  = 0x18;

		// Player (ShapeBase) world-space position. Three little-endian floats
		// (x, y, z). RE'd in chunk 13 (#97): scanned the live Player allocation
		// for a known spawn coord. Chunk 13a/14 verification across three samples
		// of moving the character showed +0x1EC0 and +0x2060 both tracked the
		// player exactly. The lower offset is the canonical SceneObject
		// mObjToWorld translation; +0x2060 is a downstream render/last-tick
		// mirror written after the canonical transform, so reading +0x1EC0 is
		// the right source of truth for movement/edge-trigger logic.
		constexpr unsigned Player_WorldPos = 0x1EC0;
	}
}
