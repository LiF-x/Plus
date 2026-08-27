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

#include "lifx_hostile.h"
#include "server/cm_server.h"   // pulls in Con::, CmOffset, and the Torque LPVOID/S32 types
#include "server/hooks/furnace/engine_internals.h"
#include "server/hooks/character/hook_vital_process_tick.h"
#include "server/hooks/character/hook_npcdec_pack.h"   // NpcDecPack::SetLoadout (2b loadouts)
#include "server/hooks/character/hook_setanimation.h"  // AnimRemap::Register (#145 anim flood gate)
#include "server/hooks/character/hook_animal_create.h"  // AnimalCreate::LastBandit (#145 char-bind)
#include "server/hooks/character/hook_container_init.h" // ContainerInit::ReloadFreshestGraveContainer (#145 loot)

#include <windows.h>   // VirtualQuery, MEMORY_BASIC_INFORMATION (guarded reads)
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Equipment registry — CORRECTED RE (A2a, issue #125), verified by disasm of the
// real equip/loot call site at RVA 0x1F2CA1..0x1F2D17:
//
//     1401f2ca1: mov rcx,[rip] # 0x140b53908   ; table = the CmServer singleton
//     1401f2ca8: mov r8d,[rdx+0xb14]           ; key   = char id
//     1401f2cb3: call 0x14028bd30              ; FNV-1a HashTable<U32,record*>::find
//     1401f2cbe: mov rax,[rsp+0x28]            ; record = out16[0]
//     1401f2cc3: test rax,rax; je ...          ; null-check record
//     1401f2cdd: mov rdi,[rax+0x48]            ; CmPlayerEquipment* = record+0x48
//     1401f2ceb: test rdi,rdi; je ...          ; null-check equipment
//     1401f2d0a: mov rdx,[rdi+0x58]            ; slot-DB = equipment+0x58
//
// The earlier crash came from ONE bug: passing `charStats` as the accessor's
// first argument. The accessor treats arg1 as a hashtable (reads bucket count at
// arg1+0x1C0, buckets at arg1+0x1A8); charStats+0x1C0 is garbage, so the masked
// index went out of bounds. The engine passes the *singleton* `*0x140B53908`.
// With the correct table the accessor is robust to any key (a miss writes {0,0}),
// so this probe cannot OOB. Every pointer deref below is null-checked.
//
// The accessor 0x28BD30 has a sibling 0x28BC20 already wrapped as the shipped,
// known-safe Engine::Character_GetByID(charID) (same table, same FNV find). This
// probe exercises BOTH the shipped helper and a faithful replica of the loot
// path, then reports whether record+0x48 (CmPlayerEquipment) is non-null for a
// real connected player — the Gate-B/Gate-C feasibility signal.
// ----------------------------------------------------------------------------
namespace
{
	constexpr unsigned kRecordToEquip = 0x48; // CmPlayerEquipment* = record + 0x48
	constexpr unsigned kRecordToRefc  = 0x50; // refcounted member  = record + 0x50
	constexpr unsigned kEquipToSlotDB = 0x58; // slot-DB            = equipment + 0x58

	// ----------------------------------------------------------------------------
	// A2a Strategy P — per-NPC character bind (issue #125), all verified static RE:
	//
	//   charStats.vtbl[+0x40] (fn 0x0A11C0, shared by EVERY charStats class)
	//   resolves the NPC's CmCharacterInfo as:
	//       if (charStats[+0x4A9] == 0)            return null;     // "is character-backed" flag
	//       charID = charStats.vtbl[+0x08]();                       // the ONLY per-class slot
	//       if (charID == 0)                       return null;
	//       return CharacterManager(*0x140B53908).find(charID);     // == Engine::Character_GetByID
	//
	//   For an NPCDecorative, charStats.vtbl[+0x08] is the constant fn 0x2E3390
	//   (`mov eax,0xFFFFFFFE; ret`) — a hard sentinel -2 that never resolves, which
	//   is exactly why a stock NPC has null equipment / no worn loot.
	//
	// Strategy P binds a *distinct* charID per NPC by:
	//   1. cloning the NPCDecorative charStats vtable (113 slots, 0x388 bytes) ONCE,
	//      patching only slot +0x08 to a reader that returns *(charStats+0x109C);
	//   2. writing the desired charID into charStats+0x109C (the engine's own charID
	//      field — only ever written by two char-setup fns, never on the NPC ctor
	//      path, so it is free on a fresh NPC);
	//   3. installing the clone on that NPC's charStats (per-instance distinctness
	//      comes from +0x109C, so one shared clone vtable is correct);
	//   4. arming the character-backed flag (+0x4A9 = 1) and flushing the equip
	//      cache (+0x498/+0x4A0 = 0) so the next getter call rebuilds.
	//
	// This is PURE memory manipulation on the NPC's own heap object plus reuse of an
	// already-registered charID — NO engine construction calls, no exe patching. The
	// engine's existing equip getter (0x0A12E0, verified) then resolves the bound
	// CmPlayerEquipment for the render + worn-loot pipelines for free.
	// ----------------------------------------------------------------------------
	constexpr uintptr_t kNpcCharStatsVtableRva = 0x7E4388; // NPCDecorative charStats vtable (image-rel)
	constexpr size_t    kCharStatsVtableSlots  = 113;      // 0x388 bytes of fn ptrs
	constexpr size_t    kCharStatsVtableBytes  = kCharStatsVtableSlots * 8;   // 0x388
	constexpr unsigned  kCharIdGetterSlot      = 0x08;     // charStats.vtbl[+0x08] = charID getter (per-class)
	constexpr unsigned  kCharIdFieldOff        = 0x109C;   // charStats+0x109C = charID (engine's own field)
	constexpr unsigned  kCharBackedFlagOff     = 0x4A9;    // charStats+0x4A9 = "is character-backed" (1 byte)
	constexpr unsigned  kCharBackedFlag2Off    = 0x4AA;    // companion flag set by setCharacterBacked
	constexpr unsigned  kEquipCacheOff         = 0x498;    // cached CmPlayerEquipment*
	constexpr unsigned  kEquipCacheRefcOff     = 0x4A0;    // cached refcount control block

	// Is [p, p+len) committed and readable? Uses VirtualQuery so a bad pointer is
	// reported, never dereferenced — the probe can never fault.
	bool SafeReadable(const void* p, size_t len)
	{
		if (!p) return false;
		MEMORY_BASIC_INFORMATION mbi;
		const char* cur = static_cast<const char*>(p);
		const char* end = cur + len;
		while (cur < end) {
			if (VirtualQuery(cur, &mbi, sizeof(mbi)) == 0) return false;
			if (mbi.State != MEM_COMMIT) return false;
			const DWORD prot = mbi.Protect;
			const bool readable =
				(prot & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
				         PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) != 0;
			if (!readable || (prot & PAGE_NOACCESS) || (prot & PAGE_GUARD)) return false;
			cur = static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize;
		}
		return true;
	}

	// Guarded qword read: returns false (and leaves out untouched) if unreadable.
	bool SafeQword(const void* base, unsigned off, uint64_t& out)
	{
		const void* p = static_cast<const char*>(base) + off;
		if (!SafeReadable(p, 8)) return false;
		out = *reinterpret_cast<const uint64_t*>(p);
		return true;
	}

	template <typename T>
	T ReadAt(void* base, unsigned off)
	{
		return *reinterpret_cast<T*>(static_cast<char*>(base) + off);
	}

	// Loot-path accessor signature, confirmed by disasm at 0x28BD30:
	//   void* __fastcall(void* table /*singleton*/, void* out16, U32 key)
	// out16 must point at a 16-byte buffer; out16[0] is 0 on miss, the record on hit.
	using pfn_record_find = void* (__fastcall*)(void* table, void* out16, uint32_t key);

	uint32_t ParseU32(int argc, const char* argv[], int index)
	{
		if (index >= argc || argv[index] == nullptr) return 0;
		return static_cast<uint32_t>(std::strtoul(argv[index], nullptr, 0));
	}

	// Guarded hexdump: `count` qwords from base, 4 per line, marking unreadable slots.
	void DumpQwords(const char* tag, void* base, unsigned count)
	{
		if (!SafeReadable(base, 8)) { Con::Echo("[lifx-a2a]   %s @%p = UNREADABLE", tag, base); return; }
		for (unsigned i = 0; i < count; i += 4) {
			char line[256]; int n = 0;
			n += std::snprintf(line + n, sizeof(line) - n, "+0x%03X:", i * 8);
			for (unsigned j = i; j < i + 4 && j < count; ++j) {
				uint64_t v;
				if (SafeQword(base, j * 8, v)) n += std::snprintf(line + n, sizeof(line) - n, " %016llx", (unsigned long long)v);
				else                          n += std::snprintf(line + n, sizeof(line) - n, " <unmapped>      ");
			}
			Con::Echo("[lifx-a2a]   %s %s", tag, line);
		}
	}

	// Does p look like a vtable pointer into the loaded module image (real C++ obj)?
	bool InModule(uint64_t p)
	{
		const uint64_t base = ::Engine::ModuleBase();
		return p >= base && p < base + 0x01000000; // image ~12MB
	}

	// Read a u32 at base+off, guarded. Returns 0 if unreadable.
	uint32_t SafeU32(const void* base, unsigned off)
	{
		const void* p = static_cast<const char*>(base) + off;
		if (!SafeReadable(p, 4)) return 0;
		return *reinterpret_cast<const uint32_t*>(p);
	}

	// Dump + characterize a CmPlayerEquipment candidate: count@+0x50, slot-DB@+0x58
	// (the slot-DB is the polymorphic part — its [+0] is a real vtable).
	void ReportEquip(const char* label, void* equip)
	{
		if (!equip) { Con::Echo("[lifx-a2a]   %s = NULL (cache cold / no equipment)", label); return; }
		if (!SafeReadable(equip, 0x60)) { Con::Echo("[lifx-a2a]   %s = %p UNREADABLE", label, equip); return; }
		const uint32_t count  = SafeU32(equip, 0x50);          // slot count (expect 0..0x11)
		uint64_t slotDB = 0; SafeQword(equip, 0x58, slotDB);   // slot-DB pointer
		uint64_t sdbVt  = 0; if (slotDB) SafeQword(reinterpret_cast<void*>(slotDB), 0, sdbVt);
		Con::Echo("[lifx-a2a]   %s = %p   count(+0x50)=%u   slotDB(+0x58)=%016llx",
		          label, equip, count, (unsigned long long)slotDB);
		Con::Echo("[lifx-a2a]       slotDB.vtable[+0]=%016llx %s",
		          (unsigned long long)sdbVt, InModule(sdbVt) ? "[REAL slot-DB object]" : "[no vtable]");
		DumpQwords(label, equip, 14);  // 0x00..0x70 of the equipment object
	}

	// Reader thunk installed in cloned vtable slot +0x08. The engine calls
	// charStats.vtbl[+0x08] as `call [rax+8]` with rcx = charStats, expecting the
	// charID in eax. On Win64 the first arg arrives in rcx, so this matches exactly.
	// Mirrors the Player getter's fallback (`mov eax,[rcx+0x109c]; ret`).
	uint32_t NpcCharIdReader(void* charStats)
	{
		return *reinterpret_cast<const uint32_t*>(static_cast<const char*>(charStats) + kCharIdFieldOff);
	}

	// Build (once) a clone of the NPCDecorative charStats vtable with slot +0x08
	// repointed at NpcCharIdReader. The clone is shared by every bound NPC — the
	// only patched slot reads per-instance memory, so it carries no instance state.
	// MSVC vtables store the RTTI complete-object-locator at [vtable-8]; we copy
	// from realVt-8 so dynamic_cast/typeid on a bound charStats still behaves.
	void* ClonedCharStatsVtable()
	{
		static void* clone = nullptr;
		if (clone) return clone;
		const uintptr_t realVt = ::Engine::ModuleBase() + kNpcCharStatsVtableRva;
		const void* src = reinterpret_cast<const void*>(realVt - 8);
		const size_t bytes = 8 + kCharStatsVtableBytes;      // RTTI slot + 113 method slots
		if (!SafeReadable(src, bytes)) return nullptr;
		void* buf = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!buf) return nullptr;
		std::memcpy(buf, src, bytes);
		void* newVt = static_cast<char*>(buf) + 8;           // object stores ptr-to-first-method
		*reinterpret_cast<void**>(static_cast<char*>(newVt) + kCharIdGetterSlot) =
			reinterpret_cast<void*>(&NpcCharIdReader);
		clone = newVt;
		return clone;
	}

	// %npc.lifxDumpNpc()  (SimObject method — obj = the NPC SimObject*)
	//
	// READ-ONLY Increment-1 probe. Confirms the Strategy-P model on a LIVE
	// NPCDecorative before any write: dumps its charStats vtable (expect the
	// NPCDecorative charStats vtable), the character-backed flag (+0x4A9, expect 0),
	// the charID field (+0x109C), the linked char-obj the Player getter reads
	// (charStats-0x728 = obj+0x380), and the equip cache (+0x498/+0x4A0). Every
	// deref is VirtualQuery-guarded, so this cannot fault even on a non-NPC object.
	void DumpNpcCb(LPVOID obj, S32 /*argc*/, const char* /*argv*/[])
	{
		if (!obj) {
			Con::Warning("lifxDumpNpc: call as a method on an object, e.g. %%npc.lifxDumpNpc()");
			return;
		}
		void* cs = static_cast<char*>(obj) + ::Engine::kCharStatsToPlayerDelta;  // +0xAA8
		const uint64_t expectVt = ::Engine::ModuleBase() + kNpcCharStatsVtableRva;

		uint64_t vt = 0; SafeQword(cs, 0, vt);
		Con::Echo("[lifx-a2a] dumpNpc obj=%p  charStats=%p", obj, cs);
		Con::Echo("[lifx-a2a]   charStats.vtbl=%016llx  expect(NPCDecorative)=%016llx  %s",
		          (unsigned long long)vt, (unsigned long long)expectVt,
		          vt == expectVt ? "[MATCH - bindable NPC]" : "[mismatch - not a stock NPCDecorative]");
		Con::Echo("[lifx-a2a]   +0x4A9 char-backed flag=%u   +0x4AA=%u",
		          SafeU32(cs, kCharBackedFlagOff) & 0xFF, SafeU32(cs, kCharBackedFlag2Off) & 0xFF);
		Con::Echo("[lifx-a2a]   +0x109C charID field=%u (0x%08x)",
		          SafeU32(cs, kCharIdFieldOff), SafeU32(cs, kCharIdFieldOff));
		const void* linkedP = static_cast<const char*>(cs) - 0x728;  // charStats-0x728 = obj+0x380
		uint64_t linked = 0;
		if (SafeReadable(linkedP, 8)) linked = *reinterpret_cast<const uint64_t*>(linkedP);
		Con::Echo("[lifx-a2a]   charStats-0x728 (Player-getter linked obj)=%016llx %s",
		          (unsigned long long)linked, linked ? "(non-null)" : "(null - safe)");
		uint64_t eq = 0, rc = 0; SafeQword(cs, kEquipCacheOff, eq); SafeQword(cs, kEquipCacheRefcOff, rc);
		Con::Echo("[lifx-a2a]   equip cache +0x498=%016llx  +0x4A0=%016llx",
		          (unsigned long long)eq, (unsigned long long)rc);

		// World transform = SceneObject mObjToWorld MatrixF (64B) at obj+0x278.
		// Torque MatrixF is row-major; translation = m[3], m[7], m[11].
		const void* mat = static_cast<const char*>(obj) + 0x278;
		if (SafeReadable(mat, 64)) {
			const float* m = reinterpret_cast<const float*>(mat);
			Con::Echo("[lifx-a2a]   world pos (obj+0x278 m[3,7,11]) = (%.2f, %.2f, %.2f) %s",
			          m[3], m[7], m[11],
			          (m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f) ? "[AT ORIGIN - position field did NOT apply!]" : "");
		} else {
			Con::Echo("[lifx-a2a]   world transform @ obj+0x278 UNREADABLE");
		}
		// Net flags (0xDC): 0x10 IsGhostable, 0x4 ScopeAlways, 0x2 ScopeLocal.
		Con::Echo("[lifx-a2a]   netFlags+0xDC=0x%08x (0x10=ghostable 0x4=scopeAlways)", SafeU32(obj, 0xDC));
	}

	// %npc.lifxGhost()  (SimObject method — obj = the NPC SimObject*)
	//
	// Make a script-spawned NPCDecorative network-visible. A raw `new NPCDecorative()`
	// never acquires a LiF GID / never enters the ghost-scope set (only the C++
	// outpost/animal managers do that), so the client is never told it exists.
	// NetObject::setScopeAlways (0x54AC70) is the self-contained primitive: it sets
	// ScopeAlways (+0xDC |= 0x4), registers with the scope manager (*0x140BC8528),
	// and loops every NetConnection calling objectInScope -> emits the ghost. It is
	// SELF-GUARDING: if (netFlags+0xDC & 0x12) != 0x10 it returns immediately (no-op),
	// so calling it can never corrupt state. We observe the flags, call it, and only
	// if it no-ops do we force the precondition (set 0x10, clear 0x2) and retry.
	// Registry of NPCs we've ghosted, so we can RE-scope them whenever a client
	// (re)connects — setScopeAlways doesn't survive a fresh NetConnection, so a
	// reconnecting client otherwise can't see them. Main-thread only in practice,
	// but guarded since the re-scope is driven from the setControlObject hook.
	std::mutex                g_ghostMtx;
	std::unordered_set<void*> g_ghostNpcs;

	// SAFE liveness check: the object's primary vtable must still be the stock
	// NPCDecorative one. Guards against a despawned NPC whose address was freed
	// (or reused by a non-NPC) — never deref/scope a dangling pointer.
	bool IsLiveNpc(void* obj)
	{
		if (!obj || !SafeReadable(obj, sizeof(void*))) return false;
		const uint64_t vt = *reinterpret_cast<uint64_t*>(obj);
		return vt == (::Engine::ModuleBase() + static_cast<unsigned>(CmOffset::NPCDEC_VTABLE));
	}

	// Make a (verified-live) NPCDecorative ghost to all connections via
	// NetObject::setScopeAlways, forcing the netFlags precondition if needed.
	// Returns true once ScopeAlways(0x4) is set. No logging (called in bulk).
	bool ScopeNpc(void* obj)
	{
		if (!IsLiveNpc(obj)) return false;
		constexpr unsigned kNetFlagsOff = 0xDC;
		uint32_t* pf = reinterpret_cast<uint32_t*>(static_cast<char*>(obj) + kNetFlagsOff);
		if (!SafeReadable(pf, 4)) return false;

		using pfn_scope = void (__fastcall*)(void*);
		pfn_scope setScopeAlways = reinterpret_cast<pfn_scope>(::Engine::ModuleBase() + 0x54AC70);

		setScopeAlways(obj);                        // no-op unless (f & 0x12)==0x10
		if (!(*pf & 0x4)) {
			*pf = (*pf | 0x10) & ~0x2u;             // satisfy the precondition, retry
			setScopeAlways(obj);
		}
		return (*pf & 0x4) != 0;
	}

	void GhostNpcCb(LPVOID obj, S32 /*argc*/, const char* /*argv*/[])
	{
		if (!obj) {
			Con::Warning("lifxGhost: call as a method on an object, e.g. %%npc.lifxGhost()");
			return;
		}
		const bool ok = ScopeNpc(obj);
		if (ok) {
			{ std::lock_guard<std::mutex> lk(g_ghostMtx); g_ghostNpcs.insert(obj); }
			// Fresh NPC: clear any stale loadout left on a reused address (fixes
			// "makenpc gives leather as default") — defaults to plate (id 0).
			Hooks::NpcDecPack::SetLoadout(obj, 0);
		}
		Con::Echo("[lifx-a2a] ghostNpc obj=%p ScopeAlways=%s%s",
		          obj, ok ? "SET [ghosting]" : "FAILED",
		          ok ? " (tracked; will re-scope on client reconnect)" : "");
	}

	// %npc.lifxBindNpc(charID)  (SimObject method — obj = the NPC SimObject*)
	//
	// Increment-2: apply the Strategy-P bind. `charID` MUST already be registered in
	// the character manager (e.g. a connected player's id for the first proof, or a
	// CreateTestCharacter id later). Defensive: aborts unless the object's charStats
	// vtable is exactly the stock NPCDecorative one, and reports whether the charID
	// resolves to a CmPlayerEquipment (via the pure-read Character_GetByID route)
	// BEFORE writing anything.
	void BindNpcCb(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) {
			Con::Warning("lifxBindNpc: call as a method, e.g. %%npc.lifxBindNpc(<charID>)");
			return;
		}
		// SimObject method ABI: argv[0]=method, argv[1]=object id, user args at [2..].
		const uint32_t charID = ParseU32(argc, argv, 2);
		if (charID == 0) {
			Con::Warning("lifxBindNpc usage: %%npc.lifxBindNpc(<charID>)  (a registered character id)");
			return;
		}
		void* cs = static_cast<char*>(obj) + ::Engine::kCharStatsToPlayerDelta;

		// --- guard: must be a stock NPCDecorative charStats ----------------------
		uint64_t curVt = 0; SafeQword(cs, 0, curVt);
		const uint64_t expectVt = ::Engine::ModuleBase() + kNpcCharStatsVtableRva;
		if (curVt != expectVt) {
			Con::Warning("[lifx-a2a] bindNpc ABORT: charStats.vtbl=%016llx != NPCDecorative %016llx "
			             "(run %%npc.lifxDumpNpc() to inspect)",
			             (unsigned long long)curVt, (unsigned long long)expectVt);
			return;
		}

		// --- pre-check: does this charID resolve to equipment? (pure read) -------
		void* cci = ::Engine::Character_GetByID(charID);
		uint64_t record = 0; if (cci) SafeQword(cci, 0x370, record);
		uint64_t preEquip = 0; if (record) SafeQword(reinterpret_cast<void*>(record), kRecordToEquip, preEquip);
		Con::Echo("[lifx-a2a] bindNpc charID=%u  CmCharacterInfo=%p  record=%016llx  equip(record+0x48)=%016llx",
		          charID, cci, (unsigned long long)record, (unsigned long long)preEquip);
		if (!preEquip)
			Con::Warning("[lifx-a2a]   note: charID %u has no CmPlayerEquipment yet — bind will install but "
			             "render needs an equipped source character.", charID);

		// --- apply the bind (pure memory writes) ---------------------------------
		void* clone = ClonedCharStatsVtable();
		if (!clone) { Con::Warning("[lifx-a2a] bindNpc ABORT: could not build cloned vtable"); return; }

		*reinterpret_cast<uint32_t*>(static_cast<char*>(cs) + kCharIdFieldOff) = charID;   // per-instance charID
		*reinterpret_cast<void**>(cs) = clone;                                             // install patched vtable
		*(static_cast<uint8_t*>(cs) + kCharBackedFlagOff) = 1;                             // arm character-backed flag
		// flush equip cache so the next getter rebuilds via our charID. A fresh NPC's
		// cache is cold (null), so clearing leaks no refcount; we clear unconditionally.
		*reinterpret_cast<void**>(static_cast<char*>(cs) + kEquipCacheOff)     = nullptr;
		*reinterpret_cast<void**>(static_cast<char*>(cs) + kEquipCacheRefcOff) = nullptr;

		Con::Echo("[lifx-a2a] bindNpc OK: obj=%p charStats=%p bound to charID=%u "
		          "(vtbl cloned, flag+0x4A9=1, cache flushed). Next equip-render tick resolves equipment.",
		          obj, cs, charID);

		// --- verify the resolution path the engine will take (pure read) ---------
		ReportEquip("bound equip (resolves on next tick)", reinterpret_cast<void*>(preEquip));
	}

	// Lifx::spawnNpcDecorative(x, y, z [, dataBlock])
	//
	// Spawns a real NPCS::NPCDecorative through the engine's OWN script `new` path
	// (via Con::Evaluate) — the exact idiom the game uses for slave NPCs — so the
	// object is fully constructed, registered (gets a console id), datablock-bound,
	// onAdd'd and scene/ghost-added by the engine. This deliberately avoids hand-
	// calling the raw factory (0x2E4AB0) + registerObject (0x4304A0): NPCDecorative
	// ::onAdd tail-calls ShapeBase::onAdd (0x0FAE80), which FAILS (and deletes the
	// object) without a datablock, so a bare factory call is unsafe.
	//
	// dataBlock defaults to "NPC_slave_A" — a loaded NPCData:DefaultPlayerData with
	// a humanoid body (verified present in the _d server's npc.cs.dso). The returned
	// object id is echoed; inspect it with %id.lifxDumpNpc() and bind it with
	// %id.lifxBindNpc(<charID>).
	// Player world-space position = three little-endian floats (x,y,z) at
	// Player+0x1EC0 (the canonical SceneObject mObjToWorld translation; verified
	// across movement samples — see engine_internals.h Engine::...::Player_WorldPos).
	constexpr unsigned kPlayerWorldPos = 0x1EC0;

	// Shared spawn core: build the engine's own `new NPCDecorative(){...}` and run
	// it via Con::Evaluate. Coords are passed as strings so callers can supply
	// literals or formatted floats.
	void SpawnNpcAt(const char* x, const char* y, const char* z, const char* db)
	{
		// The LiF console TS parser is finicky and Evaluate's RETURN value is
		// unreliable (a parse error makes it echo the last string literal). So:
		// capture the new id into a global and read it via GetVariable, and end
		// the script on an assignment (never a bare expression). A canary first
		// confirms eval + GetVariable round-trips at all.
		Con::Evaluate("$LifxCanary = 6 + 1;", false, "<Lifx>");
		Con::Echo("[lifx-a2a] canary $LifxCanary=%s (expect 7)", Con::GetVariable("$LifxCanary"));

		// The `position` init-field does NOT stick (onAdd runs after fields are
		// applied and resets the transform -> object ends up at world origin). Set
		// the transform AFTER creation via setTransform("x y z axisX axisY axisZ ang").
		char script[512];
		std::snprintf(script, sizeof(script),
			"$LifxNpcId = 0; %%o = new NPCDecorative() { dataBlock = \"%s\"; }; "
			// A bare `new` lands in the per-session MissionCleanup group, which is
			// torn down on client-leave (NPC deleted -> gone on reconnect). Reparent
			// to MissionGroup (the persistent world group) so it survives; combined
			// with setScopeAlways the engine re-ghosts it to a reconnecting client
			// natively (onGhostAlwaysObjectsReceived).
			"if (isObject(%%o)) { MissionGroup.add(%%o); %%o.setTransform(\"%s %s %s 0 0 1 0\"); $LifxNpcId = %%o.getId(); }",
			db, x, y, z);
		Con::Echo("[lifx-a2a] eval: %s", script);
		Con::Evaluate(script, false, "<Lifx::spawnNpc>");

		const char* id = Con::GetVariable("$LifxNpcId");
		const bool ok = id && id[0] && std::strcmp(id, "0") != 0;
		Con::Echo("[lifx-a2a] spawnNpcDecorative db=%s pos=(%s %s %s) -> id=%s %s",
		          db, x, y, z, (id && id[0]) ? id : "(null)",
		          ok ? "" : "[FAILED - bad datablock / onAdd rejected / parse error above]");
		if (ok)
			Con::Echo("[lifx-a2a]   next: %s.lifxDumpNpc();   then  %s.lifxBindNpc(<charID>);", id, id);
	}

	// ------------------------------------------------------------------------
	// LiFx managed-spawn maintenance (A2a #125) — mirrors NPCS::OutpostBunny
	// Manager. The engine reaps an NPCDecorative when its last client ghost is
	// freed (i.e. on disconnect), so persistence = a "respawn" half: a periodic
	// maintenance pass re-creates any registered node whose NPC is gone, wired up
	// end-to-end (MissionGroup + ghost + loadout), exactly like an outpost node.
	// Driven by a self-rescheduling TS schedule() tick (see Register()).
	// ------------------------------------------------------------------------
	struct ManagedSpawn {
		std::string   x, y, z, db;
		unsigned char loadout;
		int           lastId;        // SimObjectId of the live NPC (0 = none)
	};
	std::mutex                g_managedMtx;
	std::vector<ManagedSpawn> g_managed;

	void EnsureSpawnsImpl()
	{
		std::lock_guard<std::mutex> lk(g_managedMtx);
		for (auto& m : g_managed) {
			// Idempotent per node: keep the existing NPC if still alive, else
			// build a fresh one (group + transform + ghost + loadout) in one eval.
			char script[768];
			std::snprintf(script, sizeof(script),
				"if (isObject(%d)) { $LifxSpawnId = %d; } else { "
				"%%o = new NPCDecorative() { dataBlock = \"%s\"; }; "
				"if (isObject(%%o)) { MissionGroup.add(%%o); "
				"%%o.setTransform(\"%s %s %s 0 0 1 0\"); %%o.lifxGhost(); %%o.lifxLoadout(%u); "
				"$LifxSpawnId = %%o.getId(); } else { $LifxSpawnId = 0; } }",
				m.lastId, m.lastId, m.db.c_str(), m.x.c_str(), m.y.c_str(), m.z.c_str(),
				(unsigned)m.loadout);
			Con::Evaluate(script, false, "<Lifx::ensureSpawns>");
			const char* id = Con::GetVariable("$LifxSpawnId");
			const int newId = (id && id[0]) ? std::atoi(id) : 0;
			if (newId && newId != m.lastId)
				Con::Echo("[lifx-a2a] managed spawn (re)created: db=%s loadout=%u id=%d",
				          m.db.c_str(), (unsigned)m.loadout, newId);
			m.lastId = newId;
		}
	}

	// Lifx::manageSpawn(x, y, z, dataBlock [, loadout]) — register an outpost-style
	// respawning node. The maintenance tick keeps it spawned; the engine despawns
	// it on disconnect; the tick respawns it when a player is back.
	void ManageSpawnCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 5) {
			Con::Warning("Lifx::manageSpawn usage: (x, y, z, dataBlock [, loadout])");
			return;
		}
		ManagedSpawn m;
		m.x = argv[1]; m.y = argv[2]; m.z = argv[3]; m.db = argv[4];
		m.loadout = (argc > 5) ? static_cast<unsigned char>(ParseU32(argc, argv, 5)) : 0;
		m.lastId = 0;
		{ std::lock_guard<std::mutex> lk(g_managedMtx); g_managed.push_back(std::move(m)); }
		Con::Echo("[lifx-a2a] manageSpawn: node db=%s pos=(%s %s %s) loadout=%s "
		          "(respawns while a player is present, like an outpost node)",
		          argv[4], argv[1], argv[2], argv[3], (argc > 5) ? argv[5] : "0");

		// Start the self-rescheduling maintenance tick once, lazily — we're at
		// runtime here (a player issued the command), so the Sim event queue is
		// up and schedule() is safe (unlike at ConsoleInit/Register time).
		static bool s_tickStarted = false;
		if (!s_tickStarted) {
			s_tickStarted = true;
			Con::Evaluate(
				"function lifxEnsureSpawns() { Lifx::ensureSpawns(); schedule(2000, 0, lifxEnsureSpawns); } "
				"lifxEnsureSpawns();",
				false, "<lifx-spawn-tick>");
		}
		EnsureSpawnsImpl();
	}

	// Lifx::ensureSpawns() — one maintenance pass (driven by the schedule tick).
	void EnsureSpawnsCb(LPVOID /*obj*/, S32 /*argc*/, const char* /*argv*/[])
	{
		EnsureSpawnsImpl();
	}

	// %npc.lifxAiTick([0]) — enable (default) or disable per-pack behaviour-tree
	// ticking for this NPC. NPCDecorative doesn't tick its tree natively, so after
	// %npc.setBehavior("...") call this to make the tree actually run. Inc1 probe.
	void AiTickNpcCb(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) { Con::Warning("lifxAiTick: call as a method, e.g. %%npc.lifxAiTick()"); return; }
		const bool on = (argc < 3) || (ParseU32(argc, argv, 2) != 0);
		Hooks::NpcDecPack::SetAiTick(obj, on);
		Con::Echo("[lifx-a2a] lifxAiTick: NPC obj=%p tree-tick %s (tick fires on each ghost pack)",
		          obj, on ? "ENABLED" : "disabled");
	}

	void SpawnNpcDecorativeCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const char* x  = (argc > 1) ? argv[1] : "0";
		const char* y  = (argc > 2) ? argv[2] : "0";
		const char* z  = (argc > 3) ? argv[3] : "0";
		const char* db = (argc > 4 && argv[4] && argv[4][0]) ? argv[4] : "NPC_slave_A";
		SpawnNpcAt(x, y, z, db);
	}

	// Verify the player-model NPCData exists. It MUST be defined at server startup
	// (HostileNPCs mod loadDatablocks, onServerCreated) so it transmits to clients
	// at connect. We deliberately do NOT define it at runtime here: a runtime
	// datablock is not transmitted to already-connected clients, and spawning a
	// ghost that references an unknown datablock CRASHES the client (observed).
	bool EnsurePlayerNpcDatablock()
	{
		Con::Evaluate("$LifxDbOk = isObject(NPC_player_male);", false, "<Lifx::chkPlayerNpc>");
		const char* ok = Con::GetVariable("$LifxDbOk");
		const bool defined = ok && (ok[0] == '1' || (ok[0] == 't'));
		Con::Echo("[lifx-a2a] NPC_player_male datablock isObject=%s%s",
		          (ok && ok[0]) ? ok : "(null)",
		          defined ? "" : "  [MISSING - restart server so the HostileNPCs mod defines it at startup]");
		return defined;
	}

	// Lifx::makePlayerNpc(charID) — define the player-model NPCData (if needed) and
	// spawn it beside the connected character. Validation step for equip-render:
	// confirms a player-bodied NPCDecorative spawns + renders before any equip sync.
	void MakePlayerNpcCb(LPVOID /*obj*/, S32 argc, const char* argv[]);

	// Lifx::spawnNpcAtChar(charID [, dataBlock])
	//
	// Convenience: spawn the NPCDecorative right beside a CONNECTED character (no
	// coordinate hunting). Reads the player's verified world position and offsets
	// +2 on X so the NPC stands next to them on valid terrain.
	// Shared: read a connected character's world position and spawn `db` beside them.
	void SpawnAtChar(uint32_t charID, const char* db)
	{
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) {
			Con::Warning("[lifx-a2a] spawnAtChar: charID %u not connected", charID);
			return;
		}
		void* player = static_cast<char*>(cs) - ::Engine::kCharStatsToPlayerDelta;
		const void* posP = static_cast<const char*>(player) + kPlayerWorldPos;
		if (!SafeReadable(posP, 12)) {
			Con::Warning("[lifx-a2a] spawnAtChar: player position unreadable for charID %u", charID);
			return;
		}
		const float* p = reinterpret_cast<const float*>(posP);
		char xs[32], ys[32], zs[32];
		std::snprintf(xs, sizeof(xs), "%.3f", p[0] + 2.0f);   // beside, not inside, the player
		std::snprintf(ys, sizeof(ys), "%.3f", p[1]);
		std::snprintf(zs, sizeof(zs), "%.3f", p[2]);
		Con::Echo("[lifx-a2a] spawnAtChar charID=%u db=%s player-pos=(%.2f %.2f %.2f)", charID, db, p[0], p[1], p[2]);
		SpawnNpcAt(xs, ys, zs, db);
	}

	void SpawnNpcAtCharCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charID = ParseU32(argc, argv, 1);
		if (charID == 0) {
			Con::Warning("Lifx::spawnNpcAtChar usage: (charID [, dataBlock]) - a connected character's id");
			return;
		}
		const char* db = (argc > 2 && argv[2] && argv[2][0]) ? argv[2] : "NPC_slave_A";
		SpawnAtChar(charID, db);
	}

	// Lifx::nakedNpc(charID) — tell the connected client to cull armor meshes on
	// player-model NPCs (renders them as a naked body). Fires the client RPC
	// 'LifxNaked' -> clientCmdLifxNaked() -> the detoured tmpHideAllNakedMans
	// (client DLL hook_naked_render). Run AFTER spawning/ghosting the NPC.
	void NakedNpcCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charID = ParseU32(argc, argv, 1);
		if (charID == 0) {
			Con::Warning("Lifx::nakedNpc usage: (charID) - a connected character's id");
			return;
		}
		// Resolve the client by charID and dispatch the RPC (mirrors the player-op
		// script pattern in lifx_character.cpp).
		char script[512];
		std::snprintf(script, sizeof(script),
			"if (isObject(ClientGroup)) for (%%i = 0; %%i < ClientGroup.getCount(); %%i++) { "
			"%%c = ClientGroup.getObject(%%i); "
			"if (%%c.getCharacterId() == %u) { commandToClient(%%c, 'LifxNaked'); } }",
			charID);
		Con::Evaluate(script, false, "<Lifx::nakedNpc>");
		Con::Echo("[lifx-a2a] nakedNpc: sent 'LifxNaked' RPC to charID %u's client", charID);
	}

	void MakePlayerNpcCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charID = ParseU32(argc, argv, 1);
		if (charID == 0) {
			Con::Warning("Lifx::makePlayerNpc usage: (charID) - a connected character's id");
			return;
		}
		if (!EnsurePlayerNpcDatablock()) {
			Con::Warning("[lifx-a2a] makePlayerNpc: could not define NPC_player_male datablock");
			return;
		}
		SpawnAtChar(charID, "NPC_player_male");
	}

	// Lifx::dumpCharEquip(charID)
	//
	// Safe diagnostic probe on a CONNECTED player. Resolves CmPlayerEquipment TWO
	// independent pure-read ways (verified from the getter 0x0A12E0) and checks
	// they agree:
	//   FAST:   *(charStats + 0x498)
	//   SOURCE: Character_GetByID(charID) -> +0x370 (record) -> record+0x48
	// Zero risky live calls; every deref guarded by VirtualQuery.
	void DumpCharEquipCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charID = ParseU32(argc, argv, 1);
		if (charID == 0) {
			Con::Warning("Lifx::dumpCharEquip usage: <charID> (a connected player's character id)");
			return;
		}

		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) {
			Con::Warning("Lifx::dumpCharEquip: charID %u not in registry — is the player connected?", charID);
			return;
		}
		void* player = static_cast<char*>(cs) - ::Engine::kCharStatsToPlayerDelta;

		Con::Echo("[lifx-a2a] dumpCharEquip charID=%u  player=%p  charStats=%p", charID, player, cs);

		// ---- Route 1: fast cached pointer at charStats+0x498 ------------------
		uint64_t equipFast = 0; SafeQword(cs, 0x498, equipFast);
		uint64_t refcFast  = 0; SafeQword(cs, 0x4A0, refcFast);
		Con::Echo("[lifx-a2a] [fast] charStats+0x498 equip=%016llx  +0x4A0 refc=%016llx",
		          (unsigned long long)equipFast, (unsigned long long)refcFast);
		ReportEquip("fast equip", reinterpret_cast<void*>(equipFast));

		// ---- Route 2: source of truth via shipped Character_GetByID ----------
		void* cmCharInfo = ::Engine::Character_GetByID(charID);   // safe, shipped
		uint64_t vt = 0; if (cmCharInfo) SafeQword(cmCharInfo, 0, vt);
		Con::Echo("[lifx-a2a] [src] CmCharacterInfo=%p vtable@+0=%016llx %s",
		          cmCharInfo, (unsigned long long)vt, InModule(vt) ? "[REAL]" : "[?]");
		uint64_t record = 0; if (cmCharInfo) SafeQword(cmCharInfo, 0x370, record);
		Con::Echo("[lifx-a2a] [src] CmCharacterInfo+0x370 record=%016llx", (unsigned long long)record);
		uint64_t equipSlow = 0; if (record) SafeQword(reinterpret_cast<void*>(record), 0x48, equipSlow);
		Con::Echo("[lifx-a2a] [src] record+0x48 equip=%016llx", (unsigned long long)equipSlow);
		ReportEquip("src equip", reinterpret_cast<void*>(equipSlow));

		// ---- Verdict ---------------------------------------------------------
		if (equipFast && equipSlow && equipFast == equipSlow)
			Con::Echo("[lifx-a2a] VERDICT: both routes agree -> CmPlayerEquipment=%016llx  CHAIN VERIFIED",
			          (unsigned long long)equipFast);
		else if (equipFast || equipSlow)
			Con::Echo("[lifx-a2a] VERDICT: routes DIFFER (fast=%016llx src=%016llx) - needs reconciling",
			          (unsigned long long)equipFast, (unsigned long long)equipSlow);
		else
			Con::Echo("[lifx-a2a] VERDICT: no equipment found either way (is the player fully spawned/equipped?)");
	}

	// %npc.lifxLoadout(<id>) — assign which equipment LOADOUT this NPC renders
	// (2b route B). The id is sent to clients in OnPackUpdate; the client maps it
	// to a mesh set (lifx_loadouts.h). 0=plate, 1=leather. Takes effect on the
	// next ghost update (no respawn needed).
	void LoadoutNpcCb(LPVOID obj, S32 argc, const char* argv[])
	{
		if (!obj) {
			Con::Warning("lifxLoadout: call as a method, e.g. %%npc.lifxLoadout(1)");
			return;
		}
		// argv[0]=method, argv[1]=object id, user arg at [2].
		const uint32_t id = ParseU32(argc, argv, 2);
		Hooks::NpcDecPack::SetLoadout(obj, static_cast<unsigned char>(id));
		Con::Echo("[lifx-a2a] lifxLoadout: NPC obj=%p -> loadout %u (pushed on next ghost update)", obj, id);
	}

	// ========================================================================
	// #145 Phase 1 SPIKE — Animal-derived hostile (go/no-go gate).
	//
	// The Stage-0 HostileNPCs mod spawns a REAL Animals::Animal (datablock
	// BanditData : WolfData, shapeFile=male.dts) via spawnObject("Wolf",
	// "BanditData"). Unlike NPCDecorative, that instance is natively vital-
	// simulated, ticks its own behaviour tree, moves at full speed, takes damage
	// and dies — for free. The ONLY thing we must customise that carries real risk
	// is the death route: Animal likely sends death to a skinnable CARCASS, but we
	// want the Player worn-loot TOMBSTONE. These probes (a) pin the Animal vtable
	// + confirm the perception block, and (b) test, per-instance and reversibly,
	// whether forcing the Player death slots produces a tombstone. Run BEFORE the
	// Phase 2 build; if (b) fails irrecoverably, fall back to the NPCDecorative path.
	// ========================================================================

	// Player death-pipeline vtable slots (Player vtbl, inherited by every
	// Player-derived class incl. Animal): slot 48 = onDeathHappens orchestrator,
	// slot 44 = spawnLootstone (the worn-loot tombstone). RVAs in cm_offsets.h.
	constexpr unsigned kDeathSlot                 = 48;
	constexpr unsigned kLootstoneSlot             = 44;
	// Animals::Animal-only perception block (proves the instance really is an Animal).
	constexpr unsigned kAnimalPerceptionEnemyList = 0x2500;
	constexpr unsigned kAnimalPerceptionTarget    = 0x2520;
	constexpr unsigned kAnimalPerceptionThreat    = 0x2528;

	uint64_t PrimaryVtbl(void* obj)
	{
		uint64_t vt = 0; SafeQword(obj, 0, vt);
		return vt;
	}

	// %animal.lifxDumpAnimal()  (SimObject method) — READ-ONLY Phase-1a probe.
	// Pins the Animal primary-vtable RVA, confirms the AI tree + Animal perception
	// block, reads charStats, and reports whether Animal OVERRIDES the Player death
	// slots (48/44) or inherits them. Every deref is VirtualQuery-guarded.
	void DumpAnimalCb(LPVOID obj, S32 /*argc*/, const char* /*argv*/[])
	{
		if (!obj) { Con::Warning("lifxDumpAnimal: call as a method, e.g. %%bandit.lifxDumpAnimal()"); return; }
		const uint64_t base   = ::Engine::ModuleBase();
		const uint64_t vt     = PrimaryVtbl(obj);
		const uint64_t npcDec = base + static_cast<unsigned>(CmOffset::NPCDEC_VTABLE);
		Con::Echo("[lifx-spike] dumpAnimal obj=%p  primary.vtbl=%016llx  RVA=0x%llX  %s",
		          obj, (unsigned long long)vt, (unsigned long long)(vt - base),
		          vt == npcDec ? "[!! NPCDecorative, NOT an Animal]" :
		          (InModule(vt) ? "[in-module - candidate ANIMAL_VTABLE]" : "[NOT in module - bad obj?]"));

		uint64_t tree = 0; SafeQword(obj, static_cast<unsigned>(CmOffset::NPC_AI_TREE_OFF), tree);
		Con::Echo("[lifx-spike]   +0x24B8 AI-tree=%016llx %s", (unsigned long long)tree,
		          tree ? "(attached - native tick runs)" : "(null - no behavior set yet)");

		uint64_t en = 0, tg = 0, th = 0;
		SafeQword(obj, kAnimalPerceptionEnemyList, en);
		SafeQword(obj, kAnimalPerceptionTarget,    tg);
		SafeQword(obj, kAnimalPerceptionThreat,    th);
		Con::Echo("[lifx-spike]   perception +0x2500 enemyList=%016llx  +0x2520 target=%016llx  +0x2528 threat=%016llx",
		          (unsigned long long)en, (unsigned long long)tg, (unsigned long long)th);

		void* cs = static_cast<char*>(obj) + ::Engine::kCharStatsToPlayerDelta;
		uint64_t csVt = 0; SafeQword(cs, 0, csVt);
		Con::Echo("[lifx-spike]   charStats=%p  charStats.vtbl=%016llx (RVA 0x%llX)",
		          cs, (unsigned long long)csVt, (unsigned long long)(csVt ? csVt - base : 0));

		if (InModule(vt)) {
			// CORRECTION (RE'd 2026-06-25): vtbl slots 44/48 are NOT death/lootstone on
			// this build — slot 48 (0x42F010) is a getClassName debug-printer, slot 44
			// (0x434250) is a field serializer. The carcass is spawned by a death-event
			// path (Animals::SpawnControl::DeathHandler @ vtable 0x79D170), NOT the
			// animal's own vtable. Slots dumped raw for reference only.
			void** vtbl = reinterpret_cast<void**>(vt);
			uint64_t d = 0, l = 0;
			SafeQword(vtbl, kDeathSlot * 8, d);
			SafeQword(vtbl, kLootstoneSlot * 8, l);
			Con::Echo("[lifx-spike]   vtbl[44]=%016llx (RVA 0x%llX)  vtbl[48]=%016llx (RVA 0x%llX)  "
			          "(NOT death/lootstone - see DeathHandler 0x79D170; redirect is the open spike)",
			          (unsigned long long)l, (unsigned long long)(l - base),
			          (unsigned long long)d, (unsigned long long)(d - base));
		}

		const void* mat = static_cast<const char*>(obj) + 0x278;
		if (SafeReadable(mat, 64)) {
			const float* m = reinterpret_cast<const float*>(mat);
			Con::Echo("[lifx-spike]   world pos = (%.2f, %.2f, %.2f)", m[3], m[7], m[11]);
		}
	}

	// One cached clone of the Animal primary vtable, with slots 48 (onDeathHappens)
	// and 44 (spawnLootstone) forced to the PLAYER implementations so death routes
	// to the worn-loot tombstone instead of the animal carcass. Built lazily from
	// the first live Animal's vtable (the spike PINS that RVA; we don't hardcode it
	// yet). 256 slots copied (the primary vtable is ~196). RTTI lives at vtbl-8.
	void*    g_clonedAnimalVtbl = nullptr;
	uint64_t g_clonedAnimalSrc  = 0;

	void* BuildClonedAnimalVtbl(uint64_t srcVt)
	{
		if (g_clonedAnimalVtbl && g_clonedAnimalSrc == srcVt) return g_clonedAnimalVtbl;
		const uint64_t base = ::Engine::ModuleBase();
		constexpr size_t kSlots = 256;
		const void* src = reinterpret_cast<const void*>(srcVt - 8);
		const size_t bytes = 8 + kSlots * 8;
		if (!SafeReadable(src, bytes)) return nullptr;
		void* buf = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!buf) return nullptr;
		std::memcpy(buf, src, bytes);
		void* newVt = static_cast<char*>(buf) + 8;
		*reinterpret_cast<void**>(static_cast<char*>(newVt) + kDeathSlot * 8) =
			reinterpret_cast<void*>(base + static_cast<unsigned>(CmOffset::ON_DEATH_HAPPENS));
		*reinterpret_cast<void**>(static_cast<char*>(newVt) + kLootstoneSlot * 8) =
			reinterpret_cast<void*>(base + static_cast<unsigned>(CmOffset::SPAWN_LOOTSTONE));
		g_clonedAnimalVtbl = newVt;
		g_clonedAnimalSrc  = srcVt;
		return newVt;
	}

	// %animal.lifxDeathTombstone()  (SimObject method) — Phase-1b GATE probe.
	// Installs (per-instance, reversible by respawn) a cloned Animal vtable whose
	// death slots point at the Player tombstone path. Then KILL the NPC in-game and
	// observe: a Player lootstone drop (GO) vs an animal carcass (needs deeper RE).
	void DeathTombstoneCb(LPVOID obj, S32 /*argc*/, const char* /*argv*/[])
	{
		if (!obj) { Con::Warning("lifxDeathTombstone: call as a method, e.g. %%bandit.lifxDeathTombstone()"); return; }
		const uint64_t base = ::Engine::ModuleBase();
		const uint64_t vt   = PrimaryVtbl(obj);
		if (!InModule(vt)) {
			Con::Warning("[lifx-spike] deathTombstone ABORT: obj primary vtbl %016llx not in module",
			             (unsigned long long)vt);
			return;
		}
		if (vt == base + static_cast<unsigned>(CmOffset::NPCDEC_VTABLE)) {
			Con::Warning("[lifx-spike] deathTombstone ABORT: this is an NPCDecorative (already uses the "
			             "Player tombstone). Use it on an Animal (e.g. from Lifx::spawnHostile).");
			return;
		}
		if (g_clonedAnimalVtbl && vt == reinterpret_cast<uint64_t>(g_clonedAnimalVtbl)) {
			Con::Echo("[lifx-spike] deathTombstone: already installed on this object (no-op).");
			return;
		}
		// DISABLED (RE'd 2026-06-25): the original implementation cloned the Animal
		// vtable and repointed slots 44/48 at spawnLootstone/onDeathHappens. But on
		// this build those slots are a field SERIALIZER (0x434250) and a getClassName
		// PRINTER (0x42F010) — NOT death/lootstone. Installing the clone therefore
		// CORRUPTED the animal's networking/damage sync (why weapons couldn't kill it)
		// and never touched the real death path (carcass still spawned via the
		// SpawnControl::DeathHandler death-event chain). Neutralized until the real
		// corpse-spawn function is pinned (open §3.4 spike). No-op so it can't break a
		// test animal again.
		(void)base; (void)vt;
		Con::Warning("[lifx-spike] deathTombstone DISABLED: slots 44/48 were mis-RE'd "
		             "(serializer/classinfo, not death/lootstone) and installing CORRUPTS the animal. "
		             "The carcass is spawned by the SpawnControl DeathHandler death-event chain "
		             "(vtable 0x79D170) — redirecting it to a Player tombstone is the open death-RE spike.");
	}

	// #145 — REAL animal spawn via Animals::Manager::createAnimal (RVA 0x195FD0),
	// the engine's own per-animal factory (RE'd from SpawnControl::processTick).
	// spawnObject() does NOT work for animals ("non-conobject class") — they have no
	// console class; they're manager-only. createAnimal resolves the AnimalData BY
	// TYPE ID (AnimalData::GetDatablockByTypeID 0x18C790 — a typeId hash map),
	// mallocs 0x2548, ctors an Animals::Animal, sets datablock, registerObject's it,
	// and adds it to the server scene (so it ghosts natively). We read the Manager
	// singleton from the global at RVA 0xB80C90, call it, read the assigned
	// SimObjectId at obj+0x90 (set by registerObject 0x4304A0), then setTransform
	// beside the player. Default type 755 = Wolf (always registered) — ANY Animal
	// exercises the same death vtable for the gate; a distinct Bandit type id (so it
	// resolves BanditData/male.dts) comes with the type-registration spike.
	constexpr uintptr_t kAnimalsManagerGlobalRva = 0xB80C90;
	constexpr uintptr_t kCreateAnimalRva         = 0x195FD0;
	constexpr unsigned  kSimObjectIdOff          = 0x90;    // SimObject mId (u32), set by registerObject
	constexpr uint32_t  kWolfTypeId              = 755;

	// __fastcall(rcx=mgr, rdx=typeId, r8=quality, r9=id[0=auto], stack=flag) -> Animal*
	using PfnCreateAnimal = void* (__fastcall*)(void* mgr, uint32_t typeId, uint32_t quality,
	                                            int id, uint8_t flag);

	void SpawnHostileCore(uint32_t typeId, const char* x, const char* y, const char* z)
	{
		void* mgr = *reinterpret_cast<void**>(::Engine::ModuleBase() + kAnimalsManagerGlobalRva);
		if (!mgr || !SafeReadable(mgr, 8)) {
			Con::Warning("[lifx-spike] spawnHostile: Animals::Manager singleton null (RVA 0x%llX) — "
			             "nav mesh disabled / spawn system not initialized?",
			             (unsigned long long)kAnimalsManagerGlobalRva);
			return;
		}
		auto createAnimal = reinterpret_cast<PfnCreateAnimal>(::Engine::ModuleBase() + kCreateAnimalRva);
		void* animal = createAnimal(mgr, typeId, /*quality*/ 50, /*id auto*/ 0, /*flag*/ 1);
		if (!animal || !SafeReadable(animal, kSimObjectIdOff + 4)) {
			Con::Warning("[lifx-spike] spawnHostile: createAnimal(type %u) returned null "
			             "(no AnimalData registered with that type id?)", typeId);
			return;
		}
		// Gate this animal's setAnimation so the wolf tree's missing-sequence flood
		// on male.dts is suppressed (#145). Must precede any tree tick.
		Hooks::AnimRemap::Register(animal);
		const uint32_t simId = *reinterpret_cast<uint32_t*>(static_cast<char*>(animal) + kSimObjectIdOff);
		const uint64_t vt = PrimaryVtbl(animal);
		Con::Echo("[lifx-spike] spawnHostile: createAnimal(type %u) -> obj=%p  simId=%u  vtbl RVA=0x%llX",
		          typeId, animal, simId, (unsigned long long)(vt - ::Engine::ModuleBase()));
		if (simId == 0) {
			Con::Warning("[lifx-spike]   simId is 0 - object not registered; cannot position via script.");
			return;
		}
		// Position + ACTIVATE via the console. setActive(1) (Animal::setActive handler
		// FUN_1401896D0) integrates the animal into the active simulation — AI,
		// collision and COMBAT. Without it a raw-createAnimal animal renders + ticks
		// its tree but is NOT a valid weapon-damage target (confirmed: /animal-spawned
		// animals are killable because that path activates them; ours did not). Mirrors
		// the .setActive(1) that made the NPCDecorative move. #145.
		char script[256];
		std::snprintf(script, sizeof(script),
		              "%u.setTransform(\"%s %s %s 0 0 1 0\"); %u.setActive(1);", simId, x, y, z, simId);
		Con::Evaluate(script, false, "<Lifx::spawnHostile>");
		Con::Echo("[lifx-spike]   positioned + setActive(1) at (%s %s %s). Now hit it with a weapon — "
		          "it should take damage and die. #145", x, y, z);
	}

	void SpawnHostileCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charID = ParseU32(argc, argv, 1);
		const uint32_t typeId = (argc > 2) ? ParseU32(argc, argv, 2) : kWolfTypeId;
		if (charID == 0) {
			Con::Warning("Lifx::spawnHostile usage: (charID [, animalTypeId=755]) - a connected character id");
			return;
		}
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) { Con::Warning("[lifx-spike] spawnHostile: charID %u not connected", charID); return; }
		void* player = static_cast<char*>(cs) - ::Engine::kCharStatsToPlayerDelta;
		const void* posP = static_cast<const char*>(player) + kPlayerWorldPos;
		if (!SafeReadable(posP, 12)) {
			Con::Warning("[lifx-spike] spawnHostile: player position unreadable for charID %u", charID);
			return;
		}
		const float* p = reinterpret_cast<const float*>(posP);
		char xs[32], ys[32], zs[32];
		std::snprintf(xs, sizeof(xs), "%.3f", p[0] + 2.0f);
		std::snprintf(ys, sizeof(ys), "%.3f", p[1]);
		std::snprintf(zs, sizeof(zs), "%.3f", p[2]);
		Con::Echo("[lifx-spike] spawnHostile charID=%u type=%u player-pos=(%.2f %.2f %.2f)",
		          charID, typeId, p[0], p[1], p[2]);
		SpawnHostileCore(typeId, xs, ys, zs);
	}

	// ========================================================================
	// #145 Step 1 — character-bind for a Bandit Animal (enables the worn-loot
	// tombstone: the lootstone's movable_objects.OwnerID FKs character.ID, so an
	// unbound animal's -2 sentinel rolls back the insert). We mint a throwaway
	// character via CreateTestCharacter(accountId, charId) then apply the #125
	// Strategy-P bind to the ANIMAL's charStats (its vtable differs from the
	// NPCDecorative one, so we clone whatever vtable the live instance holds and
	// patch only the charID-getter slot). The death hook's char-backed gate
	// (+0x4A9==1) then routes death to the Player tombstone with a VALID OwnerID.
	// ========================================================================
	using PfnCreateTestChar = unsigned long long (__fastcall*)(uint32_t accountId, uint32_t charId);
	constexpr uintptr_t kCreateTestCharRva = 0x1D1670;

	void*    g_clonedAnimalCs    = nullptr;
	uint64_t g_clonedAnimalCsSrc = 0;

	void* ClonedAnimalCharStatsVtable(uint64_t srcVt)
	{
		if (g_clonedAnimalCs && g_clonedAnimalCsSrc == srcVt) return g_clonedAnimalCs;
		const void* src = reinterpret_cast<const void*>(srcVt - 8);
		const size_t bytes = 8 + kCharStatsVtableBytes;   // RTTI + 113 shared charStats slots
		if (!SafeReadable(src, bytes)) return nullptr;
		void* buf = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!buf) return nullptr;
		std::memcpy(buf, src, bytes);
		void* newVt = static_cast<char*>(buf) + 8;
		*reinterpret_cast<void**>(static_cast<char*>(newVt) + kCharIdGetterSlot) =
			reinterpret_cast<void*>(&NpcCharIdReader);
		g_clonedAnimalCs = newVt;
		g_clonedAnimalCsSrc = srcVt;
		return newVt;
	}

	std::atomic<uint32_t> g_banditCharCounter{0};

	// #177 — the bandit's equipped KIT drives BOTH the render (client loadout mesh set)
	// and the loot (items materialized in the grave on death), so what it wears is what
	// it drops. The weapon is recorded at mount (#175); the kit adds the armor.
	struct BanditKit {
		unsigned char         renderLoadout;   // client mesh-set id (0 = Full Plate, 1 = Leather)
		std::vector<uint32_t> armorItems;      // armor item ObjectTypeIDs to drop
	};

	// Full Plate: loadout 0 renders the Full_Plate_* mesh set, which corresponds 1:1 to
	// these objects_types rows (verified in lif_world_d): 547 Helm, 548 Breastplate,
	// 549 Vambraces, 550 Gauntlets, 551 Leggings, 552 Greaves.
	const BanditKit& CurrentBanditKit()
	{
		static const BanditKit kPlate{ 0, { 547, 548, 549, 550, 551, 552 } };
		return kPlate;
	}

	// Apply the kit at bind time: set the render loadout and record the armor items so
	// they drop on death (same materialize path as the weapon).
	void ApplyBanditKit(void* animal)
	{
		const BanditKit& kit = CurrentBanditKit();
		Hooks::NpcDecPack::SetLoadout(animal, kit.renderLoadout);     // render the armor
		for (uint32_t t : kit.armorItems)
			Lifx::Api::Hostile::RecordMountedItem(animal, t);        // drop the armor on death
		Con::Echo("[lifx-bind] applied kit to animal=%p: render loadout=%u, %zu armor item(s) will drop. #177",
		          animal, (unsigned)kit.renderLoadout, kit.armorItems.size());
	}

	bool BindAnimalChar(void* animal)
	{
		if (!animal) {
			Con::Warning("[lifx-bind] no Bandit to bind — spawn one first (e.g. /animal BanditData).");
			return false;
		}
		void* cs = static_cast<char*>(animal) + ::Engine::kCharStatsToPlayerDelta;
		uint64_t curVt = 0;
		if (!SafeQword(cs, 0, curVt) || !InModule(curVt)) {
			Con::Warning("[lifx-bind] animal charStats vtable unreadable/not-in-module (%016llx) — dead obj?",
			             (unsigned long long)curVt);
			return false;
		}
		if (curVt == ::Engine::ModuleBase() + kNpcCharStatsVtableRva) {
			Con::Warning("[lifx-bind] that object is an NPCDecorative, not an Animal — use %%npc.lifxBindNpc.");
			return false;
		}
		// All bandits bind to ONE shared throwaway character. CreateTestCharacter
		// hardcodes Name='test-aab' under a UNIQUE(Name) constraint, so only a SINGLE
		// such row can ever exist: minting a per-bandit id fails the name uniqueness,
		// the character row is never created, and the grave's OwnerID FK then fails
		// (movable_objects.OwnerID -> character.ID) so the bandit VANISHES with no tomb.
		// Loot no longer comes from this char's container (#175 materializes the mounted
		// gear straight into the grave), so the char is needed ONLY as a valid OwnerID
		// + an in-memory cci for the death trigger — one shared id (the existing
		// 'test-aab' row) satisfies both. CreateTestCharacter still loads that cci into
		// memory; its INSERT is then a harmless duplicate.
		const uint32_t charId    = 0x30000001u;   // the single shared 'test-aab' character
		const uint32_t accountId = 0x30000000u;
		auto mkchar = reinterpret_cast<PfnCreateTestChar>(::Engine::ModuleBase() + kCreateTestCharRva);
		mkchar(accountId, charId);
		Con::Echo("[lifx-bind] minted bandit character charId=%u (account %u)", charId, accountId);

		void* clone = ClonedAnimalCharStatsVtable(curVt);
		if (!clone) { Con::Warning("[lifx-bind] could not clone animal charStats vtable"); return false; }
		*reinterpret_cast<uint32_t*>(static_cast<char*>(cs) + kCharIdFieldOff) = charId;   // per-instance charID
		*reinterpret_cast<void**>(cs) = clone;                                             // patched getter
		*(static_cast<uint8_t*>(cs) + kCharBackedFlagOff) = 1;                             // char-backed flag
		*reinterpret_cast<void**>(static_cast<char*>(cs) + kEquipCacheOff)     = nullptr;  // flush equip cache
		*reinterpret_cast<void**>(static_cast<char*>(cs) + kEquipCacheRefcOff) = nullptr;
		Con::Echo("[lifx-bind] BOUND animal=%p charStats=%p -> charId=%u. Kill it: the death hook should now "
		          "drop a PLAYER lootstone (OwnerID valid). #145", animal, cs, charId);

		// #177 — equip the kit: render the armor + register it (and the weapon, at mount)
		// to drop on death. Render and loot now both derive from one definition.
		ApplyBanditKit(animal);
		return true;
	}

	void BindLastAnimalCb(LPVOID /*obj*/, S32 /*argc*/, const char* /*argv*/[])
	{
		BindAnimalChar(Hooks::AnimalCreate::LastBandit());
	}

	// ========================================================================
	// #145 Step 2 — cci-free worn-loot tombstone via direct SQL move.
	//
	// PROVEN from the live world DB (2026-06-25): a bound bandit's items are real
	// rows in its character containers (RootContainerID / EquipmentContainerID).
	// On death, the Player death trigger creates a grave (movable_objects row,
	// ObjectTypeID=1070, its own RootContainerID, OwnerID=charId) but the item
	// TRANSFER into the grave's container is cci-gated (Zed_is_dead bails "character
	// info not found" for a connection-less bandit), so the grave is empty.
	//
	// We close exactly that gap in the DB: move the bandit's char-container items
	// into the freshest grave's container. The engine's normal grave-open path
	// (SELECT ... FROM items WHERE ContainerID=<grave container>) then surfaces
	// them. No in-memory CmCharacterInfo, no engine construction calls, no crash
	// class — unlike the abandoned synthetic-cci path. See reference_lootstone_injection.
	// ========================================================================
	constexpr uint32_t kGraveObjectTypeId = 1070;   // lootstone/grave movable-object type (observed)
	constexpr unsigned kDbWorldConnIdx    = 1;      // DB_GET_WORLD_CONN index used by Zed_is_dead's deathlog

	using PfnDbGetConn = void* (__fastcall*)(uint32_t idx);
	using PfnDbExec    = unsigned char (__fastcall*)(void* conn, const char* fmt,
	                                                 const uint32_t* a, const uint32_t* b);

	// Run the grave-fill for a dead bandit charId on the current (main) thread:
	// move the char's Root+Equipment items into the grave's loot container, then
	// reload the grave's in-memory container so the loot is visible live.
	//   targetMid != 0 -> move straight into that container id (the grave container
	//                     captured at creation by the tryInit hook). No dependency on
	//                     the grave's movable_objects row being committed yet.
	//   targetMid == 0 -> resolve the freshest grave for this char via movable_objects
	//                     (fallback path, e.g. manual Lifx::dropBanditLoot).
	// Idempotent for an empty grave: re-running just re-targets the same items.
	// #175 — mounted-image -> loot. We control every image we mount on a bandit, and
	// each mount typeId IS the item ObjectTypeID (verified: 556 "Nordic Sword" exists
	// in objects_types and as a real items row). So rather than RE the image->item
	// field, we simply remember what we mounted per bandit and materialize those item
	// types into the grave on death — decoupled from the (generic/drained) minted-char
	// container. Generic: any future armor/amulet mount drops automatically.
	std::mutex                                  g_mountedMtx;
	std::unordered_map<void*, std::vector<uint32_t>>    g_mountedItems;        // live bandit -> mounted item typeIds
	std::unordered_map<uint32_t, std::vector<uint32_t>> g_pendingMountedLoot;  // charId -> snapshot taken at death

	// A freshly-dropped item, matching a verified live grave row (container 62):
	// Quality 50, Quantity 1, full Durability. ID auto-increments; FeatureID NULL.
	constexpr unsigned kDropQuality    = 50;
	constexpr unsigned kDropDurability = 30000;   // "2 digits after point" -> 300.00 (full)

	// INSERT each snapshotted mounted item as a real row in the grave's container, then
	// reload the container so the loot is live on first open. Mirrors FillBanditGraveCore's
	// DB plumbing but CREATES items (the gear the bandit carried) instead of moving the
	// minted char's container contents.
	void InsertMountedLootCore(uint32_t charId, uint32_t targetMid)
	{
		std::vector<uint32_t> items;
		{
			std::lock_guard<std::mutex> lk(g_mountedMtx);
			auto it = g_pendingMountedLoot.find(charId);
			if (it != g_pendingMountedLoot.end()) { items = it->second; g_pendingMountedLoot.erase(it); }
		}
		if (items.empty() || targetMid == 0) {
			Con::Echo("[lifx-loot] mounted-loot: nothing to materialize for charId=%u (container %u). #175",
			          charId, targetMid);
			return;
		}

		const uint64_t base = ::Engine::ModuleBase();
		auto getConn = reinterpret_cast<PfnDbGetConn>(base + static_cast<unsigned>(CmOffset::DB_GET_WORLD_CONN));
		auto dbExec  = reinterpret_cast<PfnDbExec>(base + static_cast<unsigned>(CmOffset::DB_EXEC_FORMATTED));
		void* conn = getConn(kDbWorldConnIdx);
		if (!conn) { Con::Warning("[lifx-loot] mounted-loot: no world DB connection (idx %u).", kDbWorldConnIdx); return; }

		// ONE atomic multi-row INSERT (not N statements): all rows commit together, so a
		// later reload can never see a partial set. The previous per-item loop + immediate
		// reload raced the async DB_EXEC (DBIPrimary thread) — the reload's SELECT ran after
		// only the FIRST row committed, loaded just that item, then re-cached the partial
		// state (only the helmet showed; the rest landed in the DB afterward). #175
		char sql[1024];
		int n = std::snprintf(sql, sizeof(sql),
			"INSERT INTO `items` (ContainerID, ObjectTypeID, Quality, Quantity, Durability, CreatedDurability) VALUES ");
		bool first = true;
		for (uint32_t typeId : items) {
			n += std::snprintf(sql + n, sizeof(sql) - n, "%s(%u, %u, %u, 1, %u, %u)",
			                   first ? "" : ", ", targetMid, typeId, kDropQuality, kDropDurability, kDropDurability);
			first = false;
			if (n >= static_cast<int>(sizeof(sql)) - 64) break;   // buffer guard
		}
		std::snprintf(sql + n, sizeof(sql) - n, ";");

		const uint32_t dummy = 0;
		const unsigned char ok = dbExec(conn, sql, &dummy, &dummy);
		Con::Echo("[lifx-loot] mounted-loot: bulk INSERT of %zu item(s) into grave container=%u -> exec=%u. #175",
		          items.size(), targetMid, (unsigned)ok);

		// Reload to bust the empty-at-creation cache. Now (in case the write already
		// committed) AND on a short delay — the DEFERRED pass is authoritative: by then the
		// async INSERT has landed, so tryInit re-reads ALL rows. The grave's loot container
		// itself loads ~1s after death, so a ~1.2s deferred reload is still well before the
		// looter can open it. #175
		Hooks::ContainerInit::ReloadFreshestGraveContainer(targetMid);
		Con::Evaluate("schedule(1200, 0, Lifx::reloadFreshestGrave);", false, "<lifx-loot-reload>");
	}

	void FillBanditGraveCore(uint32_t charId, uint32_t targetMid)
	{
		if (charId == 0) { Con::Warning("[lifx-loot] grave-fill: charId 0 — nothing to do."); return; }
		const uint64_t base = ::Engine::ModuleBase();
		auto getConn = reinterpret_cast<PfnDbGetConn>(base + static_cast<unsigned>(CmOffset::DB_GET_WORLD_CONN));
		auto dbExec  = reinterpret_cast<PfnDbExec>(base + static_cast<unsigned>(CmOffset::DB_EXEC_FORMATTED));

		void* conn = getConn(kDbWorldConnIdx);
		if (!conn) { Con::Warning("[lifx-loot] grave-fill: no world DB connection (idx %u).", kDbWorldConnIdx); return; }

		// We pre-format the full statement here (no '%' left in it) and hand it to the
		// engine's exec primitive with two dummy U32 ptrs (it always derefs them).
		// Only `items` is the UPDATE target; character/movable_objects are read-only,
		// so there is no "target table specified twice" conflict.
		char sql[768];
		if (targetMid != 0) {
			std::snprintf(sql, sizeof(sql),
				"UPDATE `items` i "
				"JOIN `character` c "
				"  ON (i.ContainerID = c.RootContainerID OR i.ContainerID = c.EquipmentContainerID) "
				"SET i.ContainerID = %u "
				"WHERE c.ID = %u;",
				targetMid, charId);
		} else {
			std::snprintf(sql, sizeof(sql),
				"UPDATE `items` i "
				"JOIN `character` c "
				"  ON (i.ContainerID = c.RootContainerID OR i.ContainerID = c.EquipmentContainerID) "
				"JOIN `movable_objects` mo "
				"  ON (mo.OwnerID = c.ID AND mo.ObjectTypeID = %u AND mo.RootContainerID IS NOT NULL) "
				"SET i.ContainerID = mo.RootContainerID "
				"WHERE c.ID = %u "
				"  AND mo.ID = (SELECT MAX(m2.ID) FROM `movable_objects` m2 "
				"               WHERE m2.OwnerID = c.ID AND m2.ObjectTypeID = %u);",
				kGraveObjectTypeId, charId, kGraveObjectTypeId);
		}

		const uint32_t dummy = 0;
		const unsigned char ok = dbExec(conn, sql, &dummy, &dummy);
		Con::Echo("[lifx-loot] grave-fill charId=%u targetContainer=%u -> exec=%u. #145",
		          charId, targetMid, (unsigned)ok);

		// The move only changed the DB; the grave's in-memory container was tryInit'd
		// EMPTY at creation and caches that. Force it to reload from the DB so the
		// loot is visible live (no restart).
		Hooks::ContainerInit::ReloadFreshestGraveContainer(targetMid);
	}

	// Lifx::dropBanditLoot(charId) — TS-callable manual fill (movable_objects subquery
	// path). Useful for testing after a kill; the live path is event-driven (below).
	void DropBanditLootCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charId = ParseU32(argc, argv, 1);
		if (charId == 0) { Con::Warning("Lifx::dropBanditLoot usage: (charId) — a bound bandit charId"); return; }
		FillBanditGraveCore(charId, 0);
	}

	// #175 — deferred reload target. InsertMountedLootCore schedule()s this ~1.2s after
	// the bulk INSERT so it re-runs tryInit once the async write has committed, surfacing
	// ALL the dropped items (not just the first). Reloads the freshest captured grave.
	void ReloadFreshestGraveCb(LPVOID /*obj*/, S32 /*argc*/, const char* /*argv*/[])
	{
		Hooks::ContainerInit::ReloadFreshestGraveContainer(0);   // 0 = freshest captured grave
	}

	// Pending worn-loot fill, armed by the death hook and consumed by the grave-
	// container capture. The grave's loot container loads ASYNChronously ~1s after
	// death (verified in-game), so a fixed schedule races the looter's first open.
	// Instead we fill at the instant the container is captured — which is also the
	// instant the tomb becomes openable — so the loot is always present on first open.
	std::atomic<uint32_t> g_pendingLootCharId{0};

	// ========================================================================
	// #145 Step 2 — worn-loot tombstone (equip foundation).
	//
	// The minted bandit character already has a COMPLETE DB-side state from
	// CreateTestCharacter -> Character::Create: a root container (f_createInventory),
	// an equipment container (f_createEquipment), STARTING ITEMS in inventory
	// (f_insertNewItemInventory) and allocated equipment_slots (p_allocate_equipment_slots).
	// What's missing for the death loot-resolver (FUN_140090ab0, which logs
	// "player %u has null CmPlayerEquipment / CmCharacterInfo / inventory") is the
	// IN-MEMORY load of that state:
	//   CmInventoryPlayer::init(record, cci) -> allocs CmPlayerEquipment@record+0x48
	//      + inventory@record+0x40 + builds the root container, calls
	//      CmPlayerEquipment::initialize
	//   CmPlayerEquipment::loadFromDb(equip)  -> populates slots from equipment_slots
	// (record = cci+0x370; cci = Character_GetByID(charId)).
	//
	// dumpBoundEquip is the read-only probe (what's actually loaded for a charId);
	// loadBoundEquip drives the engine's own builders to load it, then re-dumps.
	// Both are fully VirtualQuery-guarded.
	// ========================================================================
	// Registry of standalone CmCharacterInfo objects we built offline (charId->cci).
	// The engine's global char manager only registers connection-driven logins, so a
	// minted bandit char has no cci there; we build one ourselves and remember it.
	std::unordered_map<uint32_t, void*> g_builtCci;
	std::mutex                          g_builtCciMtx;

	void* LookupCci(uint32_t charId)
	{
		if (void* cci = ::Engine::Character_GetByID(charId)) return cci;  // real login, if any
		std::lock_guard<std::mutex> lk(g_builtCciMtx);
		auto it = g_builtCci.find(charId);
		return it == g_builtCci.end() ? nullptr : it->second;
	}

	using PfnEngineAlloc = void* (__fastcall*)(size_t);
	using PfnCciCtor     = void* (__fastcall*)(void* cci, uint32_t charId);
	using PfnCharLoad    = unsigned long long (__fastcall*)(void* cci);

	// Build a standalone, fully DB-loaded CmCharacterInfo for charId with NO
	// GameConnection. Mirrors the ref-count alloc in FUN_14028b050, then drives the
	// engine's own connect-time loader (CHARACTER_LOAD_INMEM = FUN_1401bb290), which
	// runs CharacterParameters::loadFromDb (stats + Root/EquipmentContainerID),
	// CmInventoryPlayer::init, inventory loadFromDb and CmPlayerEquipment::loadFromDb.
	// Returns the cci (== refc+0x10) and caches it in g_builtCci.
	void* BuildLoadedCci(uint32_t charId)
	{
		const uint64_t base = ::Engine::ModuleBase();
		auto alloc   = reinterpret_cast<PfnEngineAlloc>(base + static_cast<unsigned>(CmOffset::ENGINE_ALLOC));
		auto ctor    = reinterpret_cast<PfnCciCtor>(base + static_cast<unsigned>(CmOffset::CCI_FIELD_INIT_CTOR));
		auto loadAll = reinterpret_cast<PfnCharLoad>(base + static_cast<unsigned>(CmOffset::CHARACTER_LOAD_INMEM));

		char* refc = static_cast<char*>(alloc(0x3a8));
		if (!refc) { Con::Warning("[lifx-equip] BuildLoadedCci: alloc(0x3a8) failed"); return nullptr; }
		// The field-init ctor uses move-assignment (reads+decrefs the OLD field value
		// before overwriting). The engine's allocator returns zeroed memory; ours does
		// not, so we must clear the block first or the ctor decrefs garbage and crashes.
		std::memset(refc, 0, 0x3a8);
		*reinterpret_cast<uint64_t*>(refc)        = base + static_cast<unsigned>(CmOffset::CCI_REFCOUNT_VFTABLE_RVA);
		*reinterpret_cast<uint32_t*>(refc + 0x08) = 1;  // strong refcount
		*reinterpret_cast<uint32_t*>(refc + 0x0c) = 1;  // weak refcount
		void* cci = refc + 0x10;

		ctor(cci, charId);   // field-init; stores charId @ cci+0x358
		const uint32_t stored = SafeU32(cci, static_cast<unsigned>(CmOffset::CCI_CHARID_OFF));
		Con::Echo("[lifx-equip] BuildLoadedCci(%u): cci=%p refc=%p charId@0x358=%u",
		          charId, cci, static_cast<void*>(refc), stored);
		const unsigned long long lr = loadAll(cci);   // full DB load
		Con::Echo("[lifx-equip] BuildLoadedCci(%u): CHARACTER_LOAD_INMEM -> %llu", charId,
		          static_cast<unsigned long long>(lr));
		{
			std::lock_guard<std::mutex> lk(g_builtCciMtx);
			g_builtCci[charId] = cci;
		}
		return cci;
	}

	void DumpBoundEquipCore(uint32_t charId)
	{
		const uint64_t base = ::Engine::ModuleBase();
		void* cci = LookupCci(charId);
		Con::Echo("[lifx-equip] dumpBoundEquip charId=%u  CmCharacterInfo=%p %s",
		          charId, cci, cci ? "" : "[NULL - char not loaded in memory; mint may not register a cci]");
		if (!cci || !SafeReadable(cci, 8)) return;
		uint64_t cciVt = 0; SafeQword(cci, 0, cciVt);
		Con::Echo("[lifx-equip]   cci.vtbl=%016llx %s", (unsigned long long)cciVt,
		          InModule(cciVt) ? "[REAL]" : "[?]");

		uint64_t record = 0; SafeQword(cci, static_cast<unsigned>(CmOffset::CCI_RECORD_OFF), record);
		Con::Echo("[lifx-equip]   cci+0x370 record=%016llx", (unsigned long long)record);
		if (!record || !SafeReadable(reinterpret_cast<void*>(record), 0x60)) {
			Con::Echo("[lifx-equip]   record null/unreadable — nothing loaded.");
			return;
		}
		void* rec = reinterpret_cast<void*>(record);
		uint64_t inv = 0, equip = 0, refc = 0;
		SafeQword(rec, static_cast<unsigned>(CmOffset::RECORD_INVENTORY_OFF), inv);
		SafeQword(rec, static_cast<unsigned>(CmOffset::RECORD_EQUIP_OFF),     equip);
		SafeQword(rec, static_cast<unsigned>(CmOffset::RECORD_EQUIP_REFC_OFF),refc);
		Con::Echo("[lifx-equip]   inventory(+0x40)=%016llx  equip(+0x48)=%016llx  refc(+0x50)=%016llx",
		          (unsigned long long)inv, (unsigned long long)equip, (unsigned long long)refc);
		if (!equip || !SafeReadable(reinterpret_cast<void*>(equip), 0x60)) {
			Con::Echo("[lifx-equip]   CmPlayerEquipment NOT built yet -> run Lifx::loadBoundEquip(%u). "
			          "(this is why the tombstone is empty)", charId);
			return;
		}
		void* eq = reinterpret_cast<void*>(equip);
		const uint32_t playerId  = SafeU32(eq, static_cast<unsigned>(CmOffset::EQUIP_PLAYER_ID_OFF));
		uint64_t container = 0; SafeQword(eq, static_cast<unsigned>(CmOffset::EQUIP_CONTAINER_OFF), container);
		Con::Echo("[lifx-equip]   equip player_id(+0x50)=%u  container(+0x58)=%016llx",
		          playerId, (unsigned long long)container);
		if (container && SafeReadable(reinterpret_cast<void*>(container),
		                              static_cast<unsigned>(CmOffset::EQUIP_SLOT_BASE_OFF) + 0x12 * 8)) {
			// Slots 1..0x11 live at container+0x100+slot*8 (itemId low32, skinId hi32).
			unsigned nonEmpty = 0;
			for (unsigned slot = 1; slot <= 0x11; ++slot) {
				uint64_t v = 0;
				SafeQword(reinterpret_cast<void*>(container),
				          static_cast<unsigned>(CmOffset::EQUIP_SLOT_BASE_OFF) + slot * 8, v);
				if (static_cast<uint32_t>(v) != 0) {
					Con::Echo("[lifx-equip]     slot %2u: itemId=%u skinId=%u",
					          slot, static_cast<uint32_t>(v), static_cast<uint32_t>(v >> 32));
					++nonEmpty;
				}
			}
			Con::Echo("[lifx-equip]   %u equipped slot(s)%s", nonEmpty,
			          nonEmpty ? " -> these drop in the tombstone" : " (all empty; equip items via setSlot)");
		}
	}

	void DumpBoundEquipCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charId = ParseU32(argc, argv, 1);
		if (charId == 0) { Con::Warning("Lifx::dumpBoundEquip usage: (charId) — a minted/bound bandit charId"); return; }
		DumpBoundEquipCore(charId);
	}

	using PfnInvInit     = void (__fastcall*)(void* record, void* cci);
	using PfnLoadFromDb   = char (__fastcall*)(void* equip);

	void LoadBoundEquipCore(uint32_t charId, bool onMainThread)
	{
		// The cci field-init ctor (FUN_1401b9c50) registers thread-local signal state
		// that is only valid on the engine MAIN thread. Console commands run on a
		// worker thread (note the [tid] in the log differs from the {03} game thread),
		// so building there faults inside the ctor right after CmCharacterWounds.
		// We can't detect the main thread reliably (the engine's isMainThread predicate
		// caches its FIRST caller, and with no player logins the engine never primes it),
		// so ALWAYS marshal through the Sim schedule() queue — the same main-thread pump
		// ensureSpawns uses to create NPCs. The scheduled re-entry passes a 2nd arg (=1)
		// so we know we're the main-thread call and build directly instead of re-marshalling.
		if (!onMainThread) {
			Con::Echo("[lifx-equip] loadBoundEquip(%u): marshalling onto the main thread via schedule().",
			          charId);
			char script[256];
			std::snprintf(script, sizeof(script),
				"function lifxLoadEquip(%%c){ Lifx::loadBoundEquip(%%c, 1); } "
				"schedule(0, 0, lifxLoadEquip, %u);", charId);
			Con::Evaluate(script, false, "<lifx-equip-marshal>");
			return;
		}
		Con::Echo("[lifx-equip] loadBoundEquip(%u): now on the main thread — building.", charId);

		// ---- now guaranteed on the main thread: safe to construct + load ----
		// Prefer an already-present cci (real login or a prior build); otherwise build
		// a standalone one and let CHARACTER_LOAD_INMEM do the full DB load in one shot.
		void* cci = LookupCci(charId);
		if (!cci || !SafeReadable(cci, 8)) {
			Con::Echo("[lifx-equip] loadBoundEquip: no in-memory CmCharacterInfo for %u — building one offline.",
			          charId);
			cci = BuildLoadedCci(charId);
			if (!cci || !SafeReadable(cci, 8)) {
				Con::Warning("[lifx-equip] loadBoundEquip: BuildLoadedCci(%u) failed.", charId);
				return;
			}
		} else {
			// cci already exists; make sure its inventory+equipment are materialized.
			const uint64_t base = ::Engine::ModuleBase();
			uint64_t record = 0; SafeQword(cci, static_cast<unsigned>(CmOffset::CCI_RECORD_OFF), record);
			uint64_t equip  = 0;
			if (record && SafeReadable(reinterpret_cast<void*>(record), 0x60))
				SafeQword(reinterpret_cast<void*>(record), static_cast<unsigned>(CmOffset::RECORD_EQUIP_OFF), equip);
			if (!equip && record) {
				auto invInit = reinterpret_cast<PfnInvInit>(base + static_cast<unsigned>(CmOffset::CM_INVENTORY_PLAYER_INIT));
				invInit(reinterpret_cast<void*>(record), cci);
				SafeQword(reinterpret_cast<void*>(record), static_cast<unsigned>(CmOffset::RECORD_EQUIP_OFF), equip);
				if (equip && SafeReadable(reinterpret_cast<void*>(equip), 0x60)) {
					auto loadDb = reinterpret_cast<PfnLoadFromDb>(base + static_cast<unsigned>(CmOffset::EQUIP_LOAD_FROM_DB));
					loadDb(reinterpret_cast<void*>(equip));
				}
			}
		}
		Con::Echo("[lifx-equip] loadBoundEquip(%u): load complete. Dumping state:", charId);
		DumpBoundEquipCore(charId);
	}

	void LoadBoundEquipCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charId = ParseU32(argc, argv, 1);
		if (charId == 0) { Con::Warning("Lifx::loadBoundEquip usage: (charId) — a minted bandit charId"); return; }
		// argv[2]==1 means this is the schedule()-driven main-thread re-entry.
		const bool onMainThread = (argc >= 3) && (ParseU32(argc, argv, 2) == 1);
		LoadBoundEquipCore(charId, onMainThread);
	}

	// ========================================================================
	// #154 — give the bound bandit a REAL held weapon item.
	//
	// Inserts a weapon ITEM into the bandit's equipment container and points its
	// right-hand equip slot at it (world DB, same exec primitive as the worn-loot
	// fill), then loads the equipment in-memory so it takes effect live. Slot 0x0C
	// (12) is the active right-hand weapon (visible-slot table DAT_1407ade54
	// selector-0). The item also drops in the tombstone — the grave-fill already
	// sweeps the equipment container. Tests whether the male.dts hit1H_* swing (a
	// real player weapon swing) strikes natively once a weapon is held.
	//   objectTypeId = a weapon item's ObjectTypeID; slot defaults to 0x0C.
	// ========================================================================
	void EquipBanditWeaponCore(uint32_t charId, uint32_t objectTypeId, uint32_t slot)
	{
		if (charId == 0 || objectTypeId == 0) {
			Con::Warning("Lifx::equipBanditWeapon usage: (charId, objectTypeId [, slot=12])");
			return;
		}
		if (slot < 1 || slot > 0x11) slot = 0x0C;   // default: right-hand weapon slot

		const uint64_t base = ::Engine::ModuleBase();
		auto getConn = reinterpret_cast<PfnDbGetConn>(base + static_cast<unsigned>(CmOffset::DB_GET_WORLD_CONN));
		auto dbExec  = reinterpret_cast<PfnDbExec>(base + static_cast<unsigned>(CmOffset::DB_EXEC_FORMATTED));
		void* conn = getConn(kDbWorldConnIdx);
		if (!conn) { Con::Warning("[lifx-weapon] no world DB connection (idx %u).", kDbWorldConnIdx); return; }
		const uint32_t dummy = 0;
		char sql[768];

		// 1) create the weapon item inside the char's equipment container (resolved by
		//    subquery so we needn't read EquipmentContainerID back).
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO `items` (ContainerID, ObjectTypeID, Quality, Quantity, Durability, CreatedDurability) "
			"SELECT c.EquipmentContainerID, %u, 50, 1, 30000, 30000 FROM `character` c WHERE c.ID = %u;",
			objectTypeId, charId);
		const unsigned char ok1 = dbExec(conn, sql, &dummy, &dummy);

		// 2) point the equipment slot at the freshly-created item (MAX id of that type
		//    in the char's equip container).
		std::snprintf(sql, sizeof(sql),
			"UPDATE `equipment_slots` SET ItemID = ("
			"SELECT MAX(i.ID) FROM `items` i JOIN `character` c ON i.ContainerID = c.EquipmentContainerID "
			"WHERE c.ID = %u AND i.ObjectTypeID = %u) "
			"WHERE CharacterID = %u AND Slot = %u;",
			charId, objectTypeId, charId, slot);
		const unsigned char ok2 = dbExec(conn, sql, &dummy, &dummy);

		Con::Echo("[lifx-weapon] equipBanditWeapon charId=%u type=%u slot=%u -> insert=%u slot-set=%u. "
		          "Weapon is now in the bandit's equipment container (DB) and will drop in its tombstone "
		          "on death (the grave-fill sweeps the equipment container). #154",
		          charId, objectTypeId, slot, (unsigned)ok1, (unsigned)ok2);

		// NOTE: we deliberately do NOT build an in-memory CmCharacterInfo here. That path
		// (BuildLoadedCci -> CHARACTER_LOAD_INMEM) is the abandoned synthetic-cci approach;
		// it faults in CmCharacterWounds even on the main thread (the bound Animal already
		// owns this charId's live state). The DB equip alone delivers the worn-loot drop.
		// Held-weapon RENDER and the swing STRIKE are driven separately (mountImage on the
		// Animal + an explicit contact-frame _applyHit) — they don't go through this cci. #154
	}

	void EquipBanditWeaponCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t charId       = ParseU32(argc, argv, 1);
		const uint32_t objectTypeId = ParseU32(argc, argv, 2);
		const uint32_t slot         = (argc >= 4) ? ParseU32(argc, argv, 3) : 0x0Cu;
		EquipBanditWeaponCore(charId, objectTypeId, slot);
	}

	// ========================================================================
	// #154 — RENDER a held weapon on the last-spawned bandit via the engine's own
	// mount path, with NO CmCharacterInfo (so none of the crash class the equip-cci
	// build hit). Player::Mount_movable_object (MOUNT_MOVABLE_OBJECT 0xEBA30) resolves
	// a ShapeBaseImageData by movable TypeID and mounts it (mountImage, vtbl +0x3C0).
	// It is called with param_1 = obj + 0xAA8 (the charStats subobject); internally it
	// derefs param_1 - 0xAA8 = the Animal as `this`. A bad typeId logs
	// "Can't find ShapeBaseImageData for movable TypeID=%u" and returns — safe to probe.
	//   typeId candidates: WeaponData datablock ids (PracticeSword=4, NordicSword=5,
	//   KnightSword=6, Scimitar=8, ...) and/or weapon movable-object type ids.
	// We validate the Animal's vtable + the mountImage slot are in-module before
	// handing control to the engine, to avoid a blind bad-vtable call.
	// ========================================================================
	using PfnMountMovable = void (__fastcall*)(void* charStatsLike, uint32_t movableTypeId);

	void MountBanditWeaponCore(uint32_t movableTypeId)
	{
		void* animal = Hooks::AnimalCreate::LastBandit();
		if (!animal || !SafeReadable(animal, 8)) {
			Con::Warning("[lifx-weapon] mountBanditWeapon: no last bandit — spawn one via /animal BanditData first.");
			return;
		}
		uint64_t vt = 0; SafeQword(animal, 0, vt);
		if (!InModule(vt)) {
			Con::Warning("[lifx-weapon] mountBanditWeapon: animal vtable not in module (%016llx).",
			             (unsigned long long)vt);
			return;
		}
		uint64_t mountFn = 0; SafeQword(reinterpret_cast<void*>(vt), 0x3C0, mountFn);
		if (!InModule(mountFn)) {
			Con::Warning("[lifx-weapon] mountBanditWeapon: animal vtbl[+0x3C0] (mountImage) not in module (%016llx).",
			             (unsigned long long)mountFn);
			return;
		}

		const uint64_t base = ::Engine::ModuleBase();
		auto mount = reinterpret_cast<PfnMountMovable>(base + static_cast<unsigned>(CmOffset::MOUNT_MOVABLE_OBJECT));
		void* charStatsLike = static_cast<char*>(animal) + ::Engine::kCharStatsToPlayerDelta;  // animal + 0xAA8

		Con::Echo("[lifx-weapon] mountBanditWeapon: animal=%p mountImage(vtbl+0x3C0)=%016llx -> "
		          "Mount_movable_object(typeId=%u). #154",
		          animal, (unsigned long long)mountFn, movableTypeId);
		mount(charStatsLike, movableTypeId);
		Con::Echo("[lifx-weapon] mountBanditWeapon: returned. No \"Can't find ShapeBaseImageData\" warning "
		          "above => the image mounted (look at the bandit's hand). #154");
	}

	void MountBanditWeaponCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const uint32_t typeId = ParseU32(argc, argv, 1);
		if (typeId == 0) { Con::Warning("Lifx::mountBanditWeapon usage: (movableTypeId) — e.g. 5 = NordicSword"); return; }
		MountBanditWeaponCore(typeId);
	}

	// ========================================================================
	// #154 STRIKE — make the bandit's swing deal damage via the engine's OWN native
	// animal melee chain (cci-free). RE conclusion: the AI swing only plays the
	// "Attack_Fast"/"Attack_Power" animation (ANIMAL_SWING); the real hit is
	// Animals::Animal::endAttack (ANIMAL_END_ATTACK), normally fired by the per-tick
	// gate 0x18BD30 when the DTS attack-sequence trigger marker hits. The male.dts
	// remap dropped that marker, so endAttack never ran => no damage. We invoke the
	// chain directly: ANIMAL_SWING(this, type) for the visual + ANIMAL_END_ATTACK(this)
	// for the cone hit-scan + damage. endAttack reads the WeaponData* already populated
	// at this+0x24f0 by onNewDataBlock at spawn, the creature datablock at +0x2530, and
	// the attack-type index at +0x24f8 (we force a valid value first to avoid an OOB
	// datablock read). It applies damage to nearby Players via victim->vtbl[+0x350]
	// (ServerCombatHitEvent) — no CmCharacterInfo, like the render path.
	// ========================================================================
	using PfnAnimalSwing     = void     (__fastcall*)(void* animalThis, int attackType, char a3, void* a4);
	using PfnAnimalEndAttack = uint64_t (__fastcall*)(void* animalThis);

	void BanditStrikeCore(int attackType, bool visualSwing)
	{
		void* animal = Hooks::AnimalCreate::LastBandit();
		if (!animal || !SafeReadable(animal, 8)) {
			Con::Warning("[lifx-strike] banditStrike: no last bandit — spawn one via /animal BanditData first.");
			return;
		}
		uint64_t vt = 0; SafeQword(animal, 0, vt);
		if (!InModule(vt)) {
			Con::Warning("[lifx-strike] banditStrike: animal vtable not in module (%016llx).", (unsigned long long)vt);
			return;
		}
		// The WeaponData* must be live for endAttack (it derefs it with no null guard).
		// NOTE: WeaponData/AnimalData are HEAP SimObjects — their pointers are NOT in the
		// module range (only their vtables are). So validate readability + a module vtable,
		// never InModule() on the object pointer itself.
		uint64_t weapon = 0; SafeQword(animal, static_cast<unsigned>(CmOffset::ANIMAL_WEAPONDATA_OFF), weapon);
		uint64_t datablk = 0; SafeQword(animal, 0x2530, datablk);
		uint64_t weaponVt = 0; if (weapon) SafeQword(reinterpret_cast<void*>(weapon), 0, weaponVt);
		// endAttack reads weapon up to ~+0x23b0 (Hit_group_* arrays) and the inner datablock
		// up to ~+0x84d0 (attack range/angle/dmg). Require those spans readable.
		if (!weapon || !SafeReadable(reinterpret_cast<void*>(weapon), 0x23b0) || !InModule(weaponVt)) {
			Con::Warning("[lifx-strike] banditStrike: bad WeaponData @this+0x24f0 (%016llx, vt=%016llx) — endAttack "
			             "would log \"animal without weapon tries to attack\". This bandit's datablock has no melee weapon.",
			             (unsigned long long)weapon, (unsigned long long)weaponVt);
			return;
		}
		uint64_t innerDb = 0; if (datablk) SafeQword(reinterpret_cast<void*>(datablk), 0, innerDb);
		if (!datablk || !innerDb || !SafeReadable(reinterpret_cast<void*>(innerDb), 0x84d4)) {
			Con::Warning("[lifx-strike] banditStrike: creature datablock @this+0x2530 missing/unreadable "
			             "(holder=%016llx inner=%016llx).", (unsigned long long)datablk, (unsigned long long)innerDb);
			return;
		}
		if (attackType < 0 || attackType > 1) attackType = 0;   // 0=fast 1=power; clamp to known indices

		const uint64_t base = ::Engine::ModuleBase();
		auto swing     = reinterpret_cast<PfnAnimalSwing>(base + static_cast<unsigned>(CmOffset::ANIMAL_SWING));
		auto endAttack = reinterpret_cast<PfnAnimalEndAttack>(base + static_cast<unsigned>(CmOffset::ANIMAL_END_ATTACK));

		if (visualSwing) {
			// Plays the animation AND sets attackType@+0x24f8 / clears consumed@+0x24fc.
			Con::Echo("[lifx-strike] banditStrike: animal=%p swing(type=%d) (visual). #154", animal, attackType);
			swing(animal, attackType, 0, nullptr);
		} else {
			// No visual: still set a valid attack-type index so endAttack's datablock
			// indexing (datablock+0x84c0 + type*0x14) stays in range.
			char* a = static_cast<char*>(animal);
			*reinterpret_cast<int*>(a + static_cast<unsigned>(CmOffset::ANIMAL_ATTACKTYPE_OFF)) = attackType;
			a[static_cast<unsigned>(CmOffset::ANIMAL_ATTACK_CONSUMED_OFF)] = 0;
		}

		Con::Echo("[lifx-strike] banditStrike: weapon=%016llx datablk=%016llx -> endAttack(). Watch a nearby "
		          "player's health + the log (\"without weapon\" => weapon gate failed). #154",
		          (unsigned long long)weapon, (unsigned long long)datablk);
		uint64_t r = endAttack(animal);
		Con::Echo("[lifx-strike] banditStrike: endAttack returned %llu (1=ran the cone hit-scan, 0=no weapon/early-out). #154",
		          (unsigned long long)r);
	}

	void BanditStrikeCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		// (optional attackType 0/1, optional visualSwing 0/1) — defaults: fast + visual.
		const int attackType = (argc >= 2) ? static_cast<int>(ParseU32(argc, argv, 1)) : 0;
		const bool visual    = (argc >= 3) ? (ParseU32(argc, argv, 2) != 0) : true;
		BanditStrikeCore(attackType, visual);
	}

	// ========================================================================
	// #154 — enumerate the live movable-image (ShapeBaseImageData) registry that
	// Mount_movable_object's resolver (FUN_140120b80) reads, so we learn the REAL
	// datablock ids (typeId=5 was rejected => not a registered image id). The hash
	// is a chained bucket array: buckets-ptr @ MOVABLE_IMG_BUCKETS_PTR, count @
	// MOVABLE_IMG_BUCKET_COUNT; node = [0]=next, +0x8=key(id), +0x10=value(image*).
	// For each entry we print the id + image* and the first readable ASCII string
	// reachable through a pointer field (datablock name / shapeFile .dts) as a hint
	// of which id is a sword. Optional arg = substring filter (e.g. "sword").
	// ========================================================================
	// Best-effort: scan obj's first ~0x140 bytes for a field that points to a
	// readable C-string and copy it out. Returns false if none found.
	bool FirstReadableStr(void* obj, char* out, size_t outsz)
	{
		for (unsigned off = 0x08; off <= 0x140; off += 8) {
			uint64_t p = 0;
			if (!SafeQword(obj, off, p) || p == 0) continue;
			void* sp = reinterpret_cast<void*>(p);
			if (!SafeReadable(sp, 4)) continue;
			const char* s = static_cast<const char*>(sp);
			// require a run of >=3 printable chars to count as a name/path
			size_t n = 0;
			while (n < outsz - 1 && SafeReadable(s + n, 1) && s[n] >= 0x20 && s[n] < 0x7f) n++;
			if (n >= 3) { std::memcpy(out, s, n); out[n] = 0; return true; }
		}
		return false;
	}

	// case-insensitive substring test (no engine dependency)
	bool StrCaseContains(const char* hay, const char* needle)
	{
		if (!needle || !needle[0]) return true;
		for (const char* h = hay; *h; ++h) {
			const char* a = h; const char* b = needle;
			while (*a && *b && (std::tolower((unsigned char)*a) == std::tolower((unsigned char)*b))) { ++a; ++b; }
			if (!*b) return true;
		}
		return false;
	}

	void DumpMovableImagesCore(const char* filter)
	{
		const uint64_t base = ::Engine::ModuleBase();
		uint64_t bucketsBase = 0, count = 0;
		SafeQword(reinterpret_cast<void*>(base + static_cast<unsigned>(CmOffset::MOVABLE_IMG_BUCKETS_PTR)), 0, bucketsBase);
		SafeQword(reinterpret_cast<void*>(base + static_cast<unsigned>(CmOffset::MOVABLE_IMG_BUCKET_COUNT)), 0, count);
		count &= 0xFFFFFFFFull;  // stored as a 32-bit count in a 64-bit slot
		if (bucketsBase == 0 || count == 0 || count > 0x100000) {
			Con::Warning("[lifx-weapon] dumpMovableImages: registry not ready (buckets=%016llx count=%llu).",
			             (unsigned long long)bucketsBase, (unsigned long long)count);
			return;
		}
		Con::Echo("[lifx-weapon] movable-image (ShapeBaseImageData) registry: %llu buckets%s%s. #154",
		          (unsigned long long)count, filter ? " filter=" : "", filter ? filter : "");
		unsigned total = 0, shown = 0;
		for (uint64_t b = 0; b < count; ++b) {
			uint64_t node = 0;
			SafeQword(reinterpret_cast<void*>(bucketsBase + b * 8), 0, node);
			unsigned guard = 0;
			while (node && guard++ < 4096) {
				void* np = reinterpret_cast<void*>(node);
				uint64_t key = 0, val = 0, next = 0;
				SafeQword(np, 0x08, key);
				SafeQword(np, 0x10, val);
				SafeQword(np, 0x00, next);
				++total;
				char name[64] = "(no-str)";
				if (val) FirstReadableStr(reinterpret_cast<void*>(val), name, sizeof(name));
				if (!filter || StrCaseContains(name, filter)) {
					Con::Echo("   id=%-6u  image=%016llx  \"%s\"",
					          (unsigned)(key & 0xFFFFFFFF), (unsigned long long)val, name);
					++shown;
				}
				node = next;
			}
		}
		Con::Echo("[lifx-weapon] dumpMovableImages: %u registered, %u shown. Mount one with "
		          "Lifx::mountBanditWeapon(<id>). #154", total, shown);
	}

	void DumpMovableImagesCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const char* filter = (argc >= 2 && argv[1] && argv[1][0]) ? argv[1] : nullptr;
		DumpMovableImagesCore(filter);
	}
}

// ----------------------------------------------------------------------------
// Re-scope all tracked ghost NPCs. Called from the setControlObject hook when a
// client's player enters the game (incl. after a RECONNECT), because
// setScopeAlways doesn't carry over to a fresh NetConnection. Prunes any entries
// whose object is no longer a live NPCDecorative (despawned / address reused).
// ----------------------------------------------------------------------------
void Lifx::Api::Hostile::ReScopeGhostNpcs()
{
	std::lock_guard<std::mutex> lk(g_ghostMtx);
	if (g_ghostNpcs.empty()) return;
	unsigned scoped = 0;
	for (auto it = g_ghostNpcs.begin(); it != g_ghostNpcs.end(); ) {
		if (!IsLiveNpc(*it)) { it = g_ghostNpcs.erase(it); continue; }
		if (ScopeNpc(*it)) ++scoped;
		++it;
	}
	if (scoped) Con::Echo("[lifx-a2a] re-scoped %u ghost NPC(s) for (re)connecting client", scoped);
}

// ----------------------------------------------------------------------------
// #145 Step 2 — schedule the grave-fill for a just-dead bandit. Called from the
// death-redirect hook (Hooks::AnimalDeath::OnCreateCorpse) right after it fires
// the Player death trigger. We defer via the engine's own schedule() so the
// grave's movable_objects row is committed before our UPDATE looks for it (the
// trigger's grave creation may complete on the DB worker). Mirrors the
// loadBoundEquip marshalling idiom: define a tiny TS wrapper, schedule it.
// ----------------------------------------------------------------------------
void Lifx::Api::Hostile::ScheduleBanditGraveFill(uint32_t charId)
{
	if (charId == 0) return;
	char script[256];
	std::snprintf(script, sizeof(script),
		"function lifxDropLoot(%%c){ Lifx::dropBanditLoot(%%c); } "
		"schedule(2500, 0, lifxDropLoot, %u);", charId);
	Con::Evaluate(script, false, "<Lifx::dropBanditLoot>");
	Con::Echo("[lifx-loot] scheduled grave-fill for bandit charId=%u in 2.5s (after grave commit). #145", charId);
}

// Immediate (synchronous) grave fill — moves the loot straight into a known grave
// container id and reloads it. Used by the capture-driven path below.
void Lifx::Api::Hostile::FillBanditGraveNow(uint32_t charId, uint32_t graveContainerMid)
{
	FillBanditGraveCore(charId, graveContainerMid);
}

// #169 — public shim over the file-internal BindAnimalChar so the animal-create
// hook can character-bind every freshly spawned bandit (no manual bindLastAnimal).
// Idempotent: re-binding an already-bound animal returns false before minting a
// second throwaway character (its charStats vtable is then the heap clone, which
// fails BindAnimalChar's in-module guard).
bool Lifx::Api::Hostile::BindHostile(void* animal)
{
	return BindAnimalChar(animal);
}

// #175 — remember an item type we mounted on a bandit, for death-time drop.
void Lifx::Api::Hostile::RecordMountedItem(void* bandit, uint32_t objectTypeId)
{
	if (!bandit || objectTypeId == 0) return;
	std::lock_guard<std::mutex> lk(g_mountedMtx);
	auto& v = g_mountedItems[bandit];
	for (uint32_t t : v) if (t == objectTypeId) return;   // de-dup
	v.push_back(objectTypeId);
}

// Arm a worn-loot fill for a just-dead bandit. The actual fill happens when the
// grave's loot container is captured (OnGraveContainerCaptured), which is the moment
// the tomb becomes openable — so the loot is in before the first open. Called by the
// death-redirect hook right before it fires the Player death trigger.
void Lifx::Api::Hostile::ArmGraveFill(uint32_t charId, void* bandit)
{
	g_pendingLootCharId.store(charId, std::memory_order_relaxed);

	// #175 — snapshot this bandit's mounted items NOW (the object is still alive; by
	// the time the grave container is captured it's been freed). Key by charId so the
	// capture handler can materialize them into the grave.
	{
		std::lock_guard<std::mutex> lk(g_mountedMtx);
		auto it = g_mountedItems.find(bandit);
		if (it != g_mountedItems.end()) {
			g_pendingMountedLoot[charId] = it->second;
			g_mountedItems.erase(it);   // object is about to die; drop the live entry
		}
	}
	Con::Echo("[lifx-loot] armed grave-fill for bandit charId=%u (fills on grave-container capture). #145", charId);
}

// Called by the container-init hook the instant a grave (type-1070) container loads.
// Materialize the bandit's mounted gear (#175) into THIS container and reload it —
// once. Clearing the pending id makes this idempotent against later re-inits.
void Lifx::Api::Hostile::OnGraveContainerCaptured(uint32_t graveContainerMid)
{
	const uint32_t charId = g_pendingLootCharId.exchange(0, std::memory_order_relaxed);
	if (charId == 0) return;   // nothing armed (or already consumed)
	Con::Echo("[lifx-loot] grave-container %u captured -> dropping mounted gear for charId=%u. #175",
	          graveContainerMid, charId);
	// #175: materialize the mounted images (weapon/armor/amulet) the bandit carried.
	// Replaces the minted-char container move (generic/empty after the charId reuse) —
	// the tomb now contains exactly what the bandit visibly had.
	InsertMountedLootCore(charId, graveContainerMid);
}

// ----------------------------------------------------------------------------
// Registration. Called by Hooks::Engine::ConsoleInit alongside the other
// Lifx::Api::*::Register() calls (see source/server/hooks_engine.cpp).
// ----------------------------------------------------------------------------
void Lifx::Api::Hostile::Register()
{
	// Phase 0 probe (#125): validate the equipment-registry read/accessor chain
	// on a real connected player before applying it to a character-bound NPC.
	Con::AddCommand("Lifx", "dumpCharEquip", &DumpCharEquipCb,
	                "(int charID) - PROBE: dump a connected player's equipment-registry state and "
	                "resolve their CmPlayerEquipment via the engine accessor (A2a #125 Phase 0).",
	                2, 2);

	// Strategy-P character bind (#125), registered as SimObject methods so the
	// callback's `obj` is the resolved NPC pointer (no Sim::findObject needed):
	//   %npc.lifxDumpNpc()          - READ-ONLY: confirm the bind model on a live NPC
	//   %npc.lifxBindNpc(<charID>)  - bind the NPC's charStats to a registered charID
	// Method ABI: argc counts argv[0]=method + argv[1]=object + user args. So a
	// no-arg method is (2,2) and a one-arg method is (3,3).
	Con::AddCommand("SimObject", "lifxDumpNpc", &DumpNpcCb,
	                "() - PROBE (read-only): dump an NPCDecorative's charStats bind state "
	                "(vtable / +0x4A9 flag / +0x109C charID / equip cache). A2a #125 Strategy P.",
	                2, 2);
	Con::AddCommand("SimObject", "lifxBindNpc", &BindNpcCb,
	                "(int charID) - bind this NPCDecorative's charStats to a registered character id "
	                "so the engine resolves its CmPlayerEquipment (equip-render + worn-loot). A2a #125 Strategy P.",
	                3, 3);
	Con::AddCommand("SimObject", "lifxGhost", &GhostNpcCb,
	                "() - force a script-spawned NPCDecorative to ghost to clients via "
	                "NetObject::setScopeAlways (Gate B visibility). A2a #125.",
	                2, 2);
	Con::AddCommand("SimObject", "lifxLoadout", &LoadoutNpcCb,
	                "(int id) - set which equipment LOADOUT this NPC renders (0=plate, 1=leather); "
	                "pushed to clients on the next ghost update. A2a #125 2b route B.",
	                3, 3);
	Con::AddCommand("SimObject", "lifxAiTick", &AiTickNpcCb,
	                "([int on=1]) - enable/disable per-pack behaviour-tree ticking for this NPC "
	                "(NPCDecorative doesn't tick its tree natively). Call after setBehavior(). A2a #125.",
	                2, 3);

	// A2a #125 managed spawns (outpost-style proximity respawn).
	Con::AddCommand("Lifx", "manageSpawn", &ManageSpawnCb,
	                "(float x, float y, float z, string dataBlock [, int loadout]) - register a "
	                "respawning NPC node: the maintenance tick keeps it spawned while a player is "
	                "present, the engine despawns it on disconnect, the tick respawns it on return "
	                "(like an outpost node). dataBlock MUST exist at server startup. A2a #125.",
	                5, 6);
	Con::AddCommand("Lifx", "ensureSpawns", &EnsureSpawnsCb,
	                "() - run one managed-spawn maintenance pass (driven by the lifxEnsureSpawns "
	                "schedule tick). A2a #125.",
	                1, 1);
	// NOTE: the maintenance tick is NOT started here. Register() runs at
	// ConsoleInit, before the Sim event queue exists, and calling schedule() then
	// crashes startup. The tick is kicked off lazily on the first manageSpawn call
	// (runtime, Sim up) — see ManageSpawnCb.

	// Spawn a real NPCDecorative on demand (engine script `new` path) so there is a
	// live entity to dump + bind. A2a #125 Strategy P.
	Con::AddCommand("Lifx", "spawnNpcDecorative", &SpawnNpcDecorativeCb,
	                "(float x, float y, float z [, string dataBlock]) - spawn an NPCS::NPCDecorative "
	                "(default datablock NPC_slave_A) at world coords; echoes its object id. A2a #125.",
	                4, 5);
	Con::AddCommand("Lifx", "spawnNpcAtChar", &SpawnNpcAtCharCb,
	                "(int charID [, string dataBlock]) - spawn an NPCS::NPCDecorative right beside a "
	                "connected character (no coords needed); echoes its object id. A2a #125.",
	                2, 3);
	Con::AddCommand("Lifx", "makePlayerNpc", &MakePlayerNpcCb,
	                "(int charID) - define a player-model NPCData (male.dts) if needed and spawn it "
	                "beside the character; player body has the armor meshes equip render needs. A2a #125.",
	                2, 2);
	Con::AddCommand("Lifx", "nakedNpc", &NakedNpcCb,
	                "(int charID) - tell the character's client to cull armor meshes on player-model "
	                "NPCs (naked body); needs the LiFx client DLL installed. A2a #125.",
	                2, 2);

	// #145 Animal-derived hostile — Phase 1 spike (go/no-go). spawnHostile drops a
	// REAL Animals::Animal (native sim/combat); the two SimObject probes pin the
	// Animal vtable and test the death->tombstone redirect before the Phase 2 build.
	Con::AddCommand("Lifx", "spawnHostile", &SpawnHostileCb,
	                "(int charID [, int animalTypeId=755]) - spawn a REAL Animals::Animal via "
	                "Animals::Manager::createAnimal beside a connected character; echoes its SimObjectId. "
	                "type 755=Wolf (gate test); a distinct Bandit type comes later. #145.",
	                2, 3);
	Con::AddCommand("SimObject", "lifxDumpAnimal", &DumpAnimalCb,
	                "() - PROBE (read-only): pin the Animal primary-vtable RVA, confirm AI tree + "
	                "perception block, and report whether Animal overrides the Player death slots (48/44). #145.",
	                2, 2);
	Con::AddCommand("SimObject", "lifxDeathTombstone", &DeathTombstoneCb,
	                "() - SPIKE: install a per-instance cloned Animal vtable routing death (slots 48/44) "
	                "to the Player tombstone path, then kill it to test tombstone-vs-carcass. Reversible. #145.",
	                2, 2);
	Con::AddCommand("Lifx", "bindLastAnimal", &BindLastAnimalCb,
	                "() - mint+bind a character to the last /animal-spawned Bandit so its death drops a "
	                "Player tombstone (valid OwnerID) instead of a carcass. Spawn via /animal BanditData first. #145.",
	                1, 1);

	// #145 Step 2 — worn-loot tombstone (equip foundation).
	Con::AddCommand("Lifx", "dumpBoundEquip", &DumpBoundEquipCb,
	                "(int charId) - PROBE (read-only): dump a bound bandit char's in-memory CmCharacterInfo / "
	                "record / inventory / CmPlayerEquipment + equipped slots. Tells you what's loaded. #145.",
	                2, 2);
	Con::AddCommand("Lifx", "loadBoundEquip", &LoadBoundEquipCb,
	                "(int charId) - build the bound char's inventory+equipment IN MEMORY "
	                "(CmInventoryPlayer::init + CmPlayerEquipment::loadFromDb) so its death drops worn loot, "
	                "then re-dump. Marshals onto the main thread via schedule(). Run after Lifx::bindLastAnimal. #145.",
	                2, 3);

	// #154 — equip a real weapon item into the bound bandit's right-hand slot (DB).
	// Makes the weapon DROP in the tombstone (grave-fill sweeps the equipment container).
	Con::AddCommand("Lifx", "equipBanditWeapon", &EquipBanditWeaponCb,
	                "(int charId, int objectTypeId [, int slot=12]) - insert a weapon ITEM into the bound "
	                "bandit's equipment container + right-hand slot (DB). The item drops in the tombstone. "
	                "Does NOT render/strike on its own (no live cci). Run after Lifx::bindLastAnimal. #154.",
	                3, 4);

	// #154 — RENDER a held weapon on the last-spawned bandit via Player::Mount_movable_object
	// (cci-free; no crash class). Probe weapon image type ids; bad ids log gracefully.
	Con::AddCommand("Lifx", "mountBanditWeapon", &MountBanditWeaponCb,
	                "(int movableTypeId) - mount a held weapon image on the last bandit (no cci). "
	                "Try WeaponData ids: 4=PracticeSword 5=NordicSword 6=KnightSword 8=Scimitar. #154.",
	                2, 2);

	// #154 STRIKE — make the bandit's swing deal damage via the native animal melee
	// chain (ANIMAL_SWING for the visual + Animals::Animal::endAttack for the hit). cci-free.
	Con::AddCommand("Lifx", "banditStrike", &BanditStrikeCb,
	                "([int attackType=0] [int visualSwing=1]) - the last bandit performs a native melee "
	                "attack: plays the swing animation and runs endAttack's cone hit-scan, damaging nearby "
	                "players. attackType 0=fast 1=power. cci-free. #154.",
	                1, 3);

	// #154 — enumerate the live ShapeBaseImageData registry to learn real held-image ids.
	Con::AddCommand("Lifx", "dumpMovableImages", &DumpMovableImagesCb,
	                "([str filter]) - list every registered movable-image (ShapeBaseImageData) id with a "
	                "name/shapeFile hint, so you can find the right held-weapon id for mountBanditWeapon. "
	                "Optional case-insensitive name filter (e.g. \"sword\"). #154.",
	                1, 2);

	// #145 Step 2 — cci-free worn-loot: move a dead bandit's char-container items
	// into its grave's container directly in the DB (the engine's own async exec).
	// Normally fired on a short schedule() from the death hook; exposed for testing.
	Con::AddCommand("Lifx", "dropBanditLoot", &DropBanditLootCb,
	                "(int charId) - move a dead bandit's Root+Equipment items into its freshest grave's "
	                "container via direct SQL, so the empty tombstone fills with loot. cci-free. #145.",
	                2, 2);

	// #175 deferred mounted-loot reload — scheduled by InsertMountedLootCore so it runs
	// after the async bulk INSERT commits; re-tryInits the freshest grave to surface all items.
	Con::AddCommand("Lifx", "reloadFreshestGrave", &ReloadFreshestGraveCb,
	                "() - re-run tryInit on the freshest captured grave container (deferred mounted-loot reload). #175",
	                1, 1);
}
