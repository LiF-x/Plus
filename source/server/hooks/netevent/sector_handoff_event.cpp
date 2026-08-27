/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "sector_handoff_event.h"
#include "server/api/t3d_console.h"
#include "server/hooks/dispatcher/dispatcher_client.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>


namespace
{
	// Template addresses captured in PR #55 + #57 RE work. Resolved at
	// runtime by adding the loaded module base. ServerUUIDEvent was
	// picked as the template because (a) its size 0x48 is small, (b)
	// its rep is fully mapped in docs/netevent_abi.md, (c) it has no
	// per-class unpack so cloning it gives us a known-good baseline
	// for everything except the slots we override.
	constexpr std::uintptr_t kServerUUIDEvent_ClassRep_RVA        = 0xBE5810; // 0x140BE5810
	constexpr std::uintptr_t kServerUUIDEvent_ClassRepVtable_RVA  = 0x8A1C08; // 0x1408A1C08
	constexpr std::uintptr_t kServerUUIDEvent_EventVftable_RVA    = 0x8A03E8; // 0x1408A03E8
	constexpr std::uintptr_t kNetClassRep_Add_RVA                 = 0x418C40; // already in cm_offsets.h, repeated here for locality

	// BitStream::writeInt — confirmed via RE pass in #68: the per-event
	// packer FUN_140540EB0 calls it to write the trailing magic sentinel
	// (`classId ^ 0xF00DBAAD`). Signature inferred from arg shape:
	// `(stream, value, numBits)`.
	constexpr std::uintptr_t kBitStream_WriteInt_RVA              = 0x086330;

	constexpr std::size_t kClassRepBytes        = 0x80;
	constexpr std::size_t kClassRepVtableSlots  = 9;
	// Chunk-9 RE finding: ServerUUIDEvent's actual vftable has 15 slots
	// (0..14), not 7 as previously assumed. The deep-vtable dump in
	// /tmp/lifx_ghidra/netevent_deep_vtable.txt + the runtime diagnostic
	// (issue #87) showed:
	//   slot 7  = pack (per-class override, confirmed empirically #68)
	//   slot 13 = process — FUN_1404e5190 calls a per-event handler and
	//             returns 1 (success byte)
	//   slot 14 = a dispatcher thunk (calls vt[0] of arg)
	// Clone 15 real engine slots; pad slot 15 up to 16 with our stub.
	constexpr std::size_t kEventVftableTemplateSlots = 15;
	constexpr std::size_t kEventVftableSlots         = 16;
	constexpr std::size_t kEventVftableSlot_Pack     = 7;
	constexpr std::size_t kEventVftableSlot_Process  = 13;
	constexpr std::uint64_t kSectorHandoffClsIx = 0x5EC70F00; // distinctive marker visible in dumper output

	// === SectorHandoffEvent wire layout (issue #66 / docs/sector_handoff_design.md §2) ====
	//
	// Layout reasoning: the engine's NetEvent base class uses bytes
	// 0x00..0x47 on the event object (vftable at +0x00, refcount at +0x18,
	// class-specific accessors at +0x40 — see ServerUUIDEvent which is
	// exactly 0x48 bytes). Our payload starts at +0x48 to avoid colliding
	// with anything the engine reads or writes through the base class.
	//
	// All multi-byte ints are little-endian (matches Torque on-wire). The
	// arrays follow the design doc fixed-field set; variable-length tails
	// (effects/equipment) wait for a later chunk that introduces
	// length-prefixed BitStream sub-protocols.
	#pragma pack(push, 1)
	struct SectorHandoffEvent
	{
		std::uint8_t  _engine_base[0x48];  // +0x00..+0x47 — owned by NetEvent base, do not touch
		std::uint8_t  proto_version;       // +0x48
		std::uint8_t  flags;               // +0x49 — bit0=mounted, bit1=has_pet, etc.
		std::uint16_t reserved0;           // +0x4A
		std::uint32_t seq;                 // +0x4C — daemon-assigned per-pair monotonic
		std::uint64_t handoff_token;       // +0x50 — random u64 idempotency key
		std::uint8_t  src_server_uuid[16]; // +0x58
		std::uint8_t  dst_server_uuid[16]; // +0x68
		std::uint32_t char_id;             // +0x78 — global `character.ID`
		std::uint32_t account_id;          // +0x7C — FK to `account.ID`
		float         pos_x;               // +0x80 — world coords
		float         pos_y;               // +0x84
		float         pos_z;               // +0x88
		float         yaw;                 // +0x8C — radians
		std::uint32_t hp_q16;              // +0x90 — Q16 fixed-point ×65536
		std::uint32_t stamina_q16;         // +0x94
		std::uint32_t hunger_q16;          // +0x98
		std::uint32_t thirst_q16;          // +0x9C
		// v2 additions (chunk 11, issue #93) — proto_version bumps to 2.
		std::uint32_t world_sector_id;     // +0xA0 — destination sector
		std::uint32_t reserved_v2;         // +0xA4 — pad, future use
		float         world_x;             // +0xA8 — world-space landing X
		float         world_y;             // +0xAC — world-space landing Y
	};
	#pragma pack(pop)

	// Lock the wire layout at compile time so chunk 6's pack/unpack can
	// rely on these offsets without re-reading the struct definition.
	static_assert(offsetof(SectorHandoffEvent, proto_version)    == 0x48, "proto_version offset");
	static_assert(offsetof(SectorHandoffEvent, flags)            == 0x49, "flags offset");
	static_assert(offsetof(SectorHandoffEvent, seq)              == 0x4C, "seq offset");
	static_assert(offsetof(SectorHandoffEvent, handoff_token)    == 0x50, "handoff_token offset");
	static_assert(offsetof(SectorHandoffEvent, src_server_uuid)  == 0x58, "src_server_uuid offset");
	static_assert(offsetof(SectorHandoffEvent, dst_server_uuid)  == 0x68, "dst_server_uuid offset");
	static_assert(offsetof(SectorHandoffEvent, char_id)          == 0x78, "char_id offset");
	static_assert(offsetof(SectorHandoffEvent, account_id)       == 0x7C, "account_id offset");
	static_assert(offsetof(SectorHandoffEvent, pos_x)            == 0x80, "pos_x offset");
	static_assert(offsetof(SectorHandoffEvent, yaw)              == 0x8C, "yaw offset");
	static_assert(offsetof(SectorHandoffEvent, hp_q16)           == 0x90, "hp_q16 offset");
	static_assert(offsetof(SectorHandoffEvent, thirst_q16)       == 0x9C, "thirst_q16 offset");
	static_assert(offsetof(SectorHandoffEvent, world_sector_id)  == 0xA0, "world_sector_id offset");
	static_assert(offsetof(SectorHandoffEvent, reserved_v2)      == 0xA4, "reserved_v2 offset");
	static_assert(offsetof(SectorHandoffEvent, world_x)          == 0xA8, "world_x offset");
	static_assert(offsetof(SectorHandoffEvent, world_y)          == 0xAC, "world_y offset");
	static_assert(sizeof(SectorHandoffEvent)                     == 0xB0, "SectorHandoffEvent total size (v2)");

	constexpr std::uint64_t kSectorHandoffSize = sizeof(SectorHandoffEvent);

	// All four structures are static globals so they outlive the engine
	// for the process lifetime. The engine's classRep list will hold a
	// pointer to g_classRep forever; freeing it would corrupt the list.
	alignas(8) std::uint8_t  g_classRep[kClassRepBytes];
	alignas(8) void*         g_classRepVtable[kClassRepVtableSlots];
	alignas(8) void*         g_eventVftable[kEventVftableSlots];

	// Per-class descriptor returned by our ClassRep vtable slot-5 override.
	// `eventReadPacket` (FUN_140541720) calls (*rep->vt[5])(rep) then reads:
	//   +0x38 int : "bad class" flag — must be 0 for the event to be accepted.
	//   +0x3C int : direction — 0=both, 1=server-only, 2=client-only.
	// All other bytes are intentionally zero; we'll extend later if the
	// engine turns out to read more fields off this struct.
	alignas(8) std::uint8_t  g_classDescriptor[0x40];

	std::atomic<bool>        g_registered{false};

	// Local typedef + slot for NetClassRep::add. We resolve it ourselves
	// rather than reusing the dumper's _NetClassRep_Add — the dumper may
	// not be enabled, and we want this code to work standalone.
	using NetClassRepAdd_Fn = void (__fastcall*)(void* rep);
	NetClassRepAdd_Fn g_netClassRepAdd = nullptr;

	// BitStream::writeInt(stream, value, numBits). Resolved at Register().
	using BSWriteInt_Fn = void (__fastcall*)(void* stream, std::uint32_t value, std::int64_t numBits);
	BSWriteInt_Fn g_bsWriteInt = nullptr;

	// === Overridden ClassRep vtable slots ====================================
	//
	// Calling conventions inferred from the receive code in PR #57:
	//   slot 7 create(rep, ?, ?, ?) -> evt*    (matches FUN_1404E5CE0)
	//   slot 8 unpack(rep, conn, stream)       (matches FUN_140541720 call site)
	//
	// `__fastcall` matches every other hook in the codebase. On x64
	// Windows, __fastcall == the default ABI, so this is effectively
	// just notation.

	void* __fastcall ourCreate(void* rep, void* /*p2*/, void* /*p3*/, void* /*p4*/)
	{
		const auto size = *reinterpret_cast<std::uint64_t*>(
			reinterpret_cast<std::uint8_t*>(rep) + 0x40);
		void* evt = ::operator new(static_cast<std::size_t>(size));
		if (!evt) return nullptr;
		// Zero-init the payload — the original `create()` calls the
		// base NetEvent ctor (FUN_14053F9A0), but for an MVP that
		// never gets sent we only need the vftable + zeroed fields.
		std::memset(evt, 0, static_cast<std::size_t>(size));
		*reinterpret_cast<void**>(evt) = g_eventVftable;
		Con::Echo("[sector-handoff] create() called -> evt @ %p (size 0x%llX)",
		          evt, (unsigned long long)size);
		return evt;
	}

	void __fastcall ourUnpack(void* rep, void* conn, void* stream)
	{
		// MVP: just log. Don't read from the stream. Sister PRs will
		// implement the real per-field BitStream reads once the design
		// in docs/sector_handoff_design.md §2 firms up.
		Con::Echo("[sector-handoff] unpack called (rep=%p conn=%p stream=%p)",
		          rep, conn, stream);
	}

	// ClassRep vtable slot 5 — returns our per-class descriptor. The
	// receive path in FUN_140541720 reads `+0x38` (must be 0 to pass the
	// "not a bad class" gate) and `+0x3C` (direction).
	void* __fastcall ourGetClassDescriptor(void* /*rep*/)
	{
		return g_classDescriptor;
	}

	// Event vftable slot 5 — defensive override so any engine code that
	// resolves an event back to its rep via `(*evt->vt[5])(evt)` lands on
	// our g_classRep, not on the &DAT_140BC3860 (EngineTypeInfo) that
	// ServerUUIDEvent's stub returns.
	void* __fastcall ourEventGetClassRep(void* /*evt*/)
	{
		return g_classRep;
	}

	// One-size-fits-all stub for the event vftable slots we pad past
	// ServerUUIDEvent's 7-slot template (issue #64). The drainer calls
	// slot 10 (pre-pack metadata), slot 11 (pack), and slot 12 (flag
	// accessor) on actively-used events. PR #57 showed all three return
	// `char` (0/1) and the call sites consume them as `if (cVar != 0)`
	// gates — returning 0 means "skip the gated branch / wrote nothing"
	// which is what we want for an empty payload. Any other slot reached
	// past slot 6 falls into the same stub.
	std::uint8_t __fastcall ourEventStub(void* /*evt*/, ...)
	{
		return 0;
	}

	// Event vftable slot 7 = pack(event, conn, stream). Per the per-event
	// packer FUN_140540EB0 from issue #68 RE: the drainer calls vt[7] on
	// the event with `(this, NetConnection*, BitStream*)`, expecting it
	// to push the class-specific payload onto the stream. The trailing
	// `magic ^ classId` sentinel is written by the drainer itself after
	// we return — we only emit the payload.
	//
	// Layout matches the SectorHandoffEvent struct above. Everything in
	// the payload region (+0x48..+0xA7) is 32-bit-aligned, so we just
	// stream it as 24 little-endian u32s. The struct's static_asserts
	// guarantee the field offsets stay aligned.
	// Event vftable slot 13 = process(event, conn). Per chunk-9 RE
	// (issue #87): the engine's slot-13 fn for ServerUUIDEvent is
	// FUN_1404e5190 which delegates to FUN_1404e6130 and returns 1.
	// For our SectorHandoff we just log the typed fields; later chunks
	// will mutate game state from here (the federation roadmap is #86).
	// Returns 1 to mimic engine convention ("handled").
	std::uint8_t __fastcall ourProcess(void* evt, void* conn)
	{
		auto* e = reinterpret_cast<const SectorHandoffEvent*>(evt);
		Con::Echo("[sector-handoff] process: evt=%p conn=%p proto=%u seq=%u char=%u pos=(%.3f, %.3f, %.3f)",
		          evt, conn, (unsigned)e->proto_version, (unsigned)e->seq,
		          (unsigned)e->char_id, e->pos_x, e->pos_y, e->pos_z);
		return 1;
	}

	void __fastcall ourPack(void* evt, void* /*conn*/, void* stream)
	{
		auto* bytes = reinterpret_cast<std::uint8_t*>(evt) + 0x48;
		auto* words = reinterpret_cast<const std::uint32_t*>(bytes);
		constexpr std::size_t kPayloadWords =
			(sizeof(SectorHandoffEvent) - 0x48) / sizeof(std::uint32_t);
		for (std::size_t i = 0; i < kPayloadWords; ++i)
		{
			g_bsWriteInt(stream, words[i], 32);
		}
	}
}

bool Hooks::SectorHandoff::Register()
{
	if (g_registered.exchange(true, std::memory_order_acq_rel)) return true;

	HMODULE mod = GetModuleHandleA(nullptr);
	if (!mod)
	{
		Con::Warning("[sector-handoff] GetModuleHandle failed");
		g_registered.store(false);
		return false;
	}
	const auto modBase = reinterpret_cast<std::uintptr_t>(mod);

	g_netClassRepAdd = reinterpret_cast<NetClassRepAdd_Fn>(modBase + kNetClassRep_Add_RVA);
	g_bsWriteInt     = reinterpret_cast<BSWriteInt_Fn>   (modBase + kBitStream_WriteInt_RVA);

	// 1) Clone the three template structures.
	std::memcpy(g_classRep,
	            reinterpret_cast<const void*>(modBase + kServerUUIDEvent_ClassRep_RVA),
	            kClassRepBytes);
	std::memcpy(g_classRepVtable,
	            reinterpret_cast<const void*>(modBase + kServerUUIDEvent_ClassRepVtable_RVA),
	            kClassRepVtableSlots * sizeof(void*));
	// Copy ServerUUIDEvent's 7 real slots, then fill the padding 7..15
	// with our safe stub so the send-drainer's calls to vt[10..12] land
	// on a known-safe return-0 function instead of running off the end
	// of the template into adjacent .rdata.
	std::memcpy(g_eventVftable,
	            reinterpret_cast<const void*>(modBase + kServerUUIDEvent_EventVftable_RVA),
	            kEventVftableTemplateSlots * sizeof(void*));

	for (std::size_t i = kEventVftableTemplateSlots; i < kEventVftableSlots; ++i)
	{
		g_eventVftable[i] = reinterpret_cast<void*>(&ourEventStub);
	}
	// Slot 7 = pack (issue #68). Drainer reaches this via vt[7] when it
	// emits an event onto the per-connection BitStream.
	g_eventVftable[kEventVftableSlot_Pack]    = reinterpret_cast<void*>(&ourPack);
	// Slot 13 = process (issue #87). Engine receive path calls this
	// after unpack; InjectFromWire calls it directly to simulate.
	g_eventVftable[kEventVftableSlot_Process] = reinterpret_cast<void*>(&ourProcess);

	// 2) Override slots 5/7/8 on the ClassRep vtable, and slot 5 on the
	//    event vftable. Inherited slots stay as the engine's defaults.
	g_classRepVtable[5] = reinterpret_cast<void*>(&ourGetClassDescriptor);
	g_classRepVtable[7] = reinterpret_cast<void*>(&ourCreate);
	g_classRepVtable[8] = reinterpret_cast<void*>(&ourUnpack);
	g_eventVftable [5]  = reinterpret_cast<void*>(&ourEventGetClassRep);

	// 2b) Zero the class descriptor — both inspected fields are required
	//     to be 0 (no bad-class flag, bidirectional). This is what the
	//     receive path will check.
	std::memset(g_classDescriptor, 0, sizeof(g_classDescriptor));

	// 3) Patch the cloned rep: own vtable, unique classIdx, own size,
	//    and clear the next-ptr so NetClassRep::add can insert us at
	//    the head cleanly.
	*reinterpret_cast<void**>          (g_classRep + 0x00) = g_classRepVtable;
	*reinterpret_cast<std::uint64_t*>  (g_classRep + 0x08) = kSectorHandoffClsIx;
	*reinterpret_cast<std::uint64_t*>  (g_classRep + 0x40) = kSectorHandoffSize;
	*reinterpret_cast<void**>          (g_classRep + 0x50) = nullptr;

	// 4) Hand the rep to the engine. If the dumper hook is also attached
	//    this call routes through our hook first — that's intentional,
	//    it lets us see our own rep in logs/netclassrep_dump.log under
	//    a `[netclassrep] live #N` entry.
	g_netClassRepAdd(g_classRep);

	Con::Echo("[sector-handoff] registered rep @ %p (classIdx=0x%llX size=0x%llX vtable=%p)",
	          g_classRep,
	          (unsigned long long)kSectorHandoffClsIx,
	          (unsigned long long)kSectorHandoffSize,
	          g_classRepVtable);

	// 5) Self-test — exercise the overridden slots without touching the
	//    engine's send/receive paths. Each line should print a "pass" or
	//    a one-line diff. If any step fails the next chunk (actual post)
	//    will land on broken ground, so we want to know now.
	using CreateFn      = void* (__fastcall*)(void*, void*, void*, void*);
	using GetClassRepFn = void* (__fastcall*)(void*);
	using GetDescFn     = void* (__fastcall*)(void*);

	auto createSlot   = reinterpret_cast<CreateFn>(g_classRepVtable[7]);
	auto repGet5      = reinterpret_cast<GetDescFn>(g_classRepVtable[5]);

	void* evt = createSlot(g_classRep, nullptr, nullptr, nullptr);
	if (!evt)
	{
		Con::Warning("[sector-handoff] self-test: create() returned null");
	}
	else
	{
		void* evtVftable = *reinterpret_cast<void**>(evt);
		Con::Echo("[sector-handoff] self-test: create() ok evt=%p", evt);
		Con::Echo("[sector-handoff] self-test: event vftable %s (got %p want %p)",
		          evtVftable == g_eventVftable ? "MATCH" : "MISMATCH",
		          evtVftable, g_eventVftable);

		auto evtGet5 = reinterpret_cast<GetClassRepFn>(g_eventVftable[5]);
		void* evtRep = evtGet5(evt);
		Con::Echo("[sector-handoff] self-test: evt->vt[5]() %s our rep (got %p want %p)",
		          evtRep == g_classRep ? "RETURNED" : "DID NOT RETURN",
		          evtRep, (void*)g_classRep);

		::operator delete(evt);
	}

	void* desc = repGet5(g_classRep);
	Con::Echo("[sector-handoff] self-test: rep->vt[5]() %s our descriptor (got %p want %p)",
	          desc == g_classDescriptor ? "RETURNED" : "DID NOT RETURN",
	          desc, (void*)g_classDescriptor);

	// 6) Wire-struct round-trip (issue #66). Allocate a typed event, fill
	//    every field with a distinct sentinel, read each back. Catches
	//    drift between SectorHandoffEvent's offsets and the chunk-6 pack
	//    code we're about to write — also surfaces any subtle alignment
	//    bug from #pragma pack(1) before we ship pack/unpack on top.
	void* roundtripEvt = createSlot(g_classRep, nullptr, nullptr, nullptr);
	if (!roundtripEvt)
	{
		Con::Warning("[sector-handoff] self-test: roundtrip create() returned null");
	}
	else
	{
		auto* e = reinterpret_cast<SectorHandoffEvent*>(roundtripEvt);
		e->proto_version    = 2;
		e->flags            = 0x03;
		e->seq              = 0xDEADBEEFu;
		e->handoff_token    = 0xCAFEF00DBADD1E55ull;
		for (int i = 0; i < 16; ++i) e->src_server_uuid[i] = std::uint8_t(0x10 + i);
		for (int i = 0; i < 16; ++i) e->dst_server_uuid[i] = std::uint8_t(0x20 + i);
		e->char_id          = 0x11112222u;
		e->account_id       = 0x33334444u;
		e->pos_x            = 1.5f;
		e->pos_y            = -2.5f;
		e->pos_z            = 100.25f;
		e->yaw              = 3.14159f;
		e->hp_q16           = 100u * 65536u;
		e->stamina_q16      = 50u  * 65536u;
		e->hunger_q16       = 30u  * 65536u;
		e->thirst_q16       = 20u  * 65536u;
		e->world_sector_id  = 451u;
		e->reserved_v2      = 0xA5A5A5A5u;
		e->world_x          = 12345.5f;
		e->world_y          = -6789.0f;

		// Read back via raw byte access at the asserted offsets so the
		// check defends both the field layout AND the static_asserts.
		auto* raw = reinterpret_cast<std::uint8_t*>(roundtripEvt);
		bool ok = true;
		ok &= (raw[0x48] == 2);
		ok &= (*reinterpret_cast<std::uint32_t*>(raw + 0x4C) == 0xDEADBEEFu);
		ok &= (*reinterpret_cast<std::uint64_t*>(raw + 0x50) == 0xCAFEF00DBADD1E55ull);
		ok &= (raw[0x58] == 0x10 && raw[0x67] == 0x1F);
		ok &= (raw[0x68] == 0x20 && raw[0x77] == 0x2F);
		ok &= (*reinterpret_cast<std::uint32_t*>(raw + 0x78) == 0x11112222u);
		ok &= (*reinterpret_cast<float*>(raw + 0x80)        == 1.5f);
		ok &= (*reinterpret_cast<float*>(raw + 0x8C)        == 3.14159f);
		ok &= (*reinterpret_cast<std::uint32_t*>(raw + 0xA0) == 451u);
		ok &= (*reinterpret_cast<float*>(raw + 0xA8)        == 12345.5f);
		ok &= (*reinterpret_cast<float*>(raw + 0xAC)        == -6789.0f);

		Con::Echo("[sector-handoff] self-test: wire struct round-trip %s (size=0x%zX)",
		          ok ? "OK" : "FAILED",
		          sizeof(SectorHandoffEvent));
		::operator delete(roundtripEvt);
	}

	return true;
}

std::uint8_t Hooks::SectorHandoff::PostTo(void* conn)
{
	if (!conn)
	{
		Con::Echo("[sector-handoff] PostTo: conn=null, skipping");
		return 0;
	}
	if (!g_registered.load(std::memory_order_acquire))
	{
		Con::Warning("[sector-handoff] PostTo: rep not yet registered");
		return 0;
	}

	// Allocate a fresh event via our slot-7 create().
	using CreateFn = void* (__fastcall*)(void*, void*, void*, void*);
	auto createSlot = reinterpret_cast<CreateFn>(g_classRepVtable[7]);
	void* evt = createSlot(g_classRep, nullptr, nullptr, nullptr);
	if (!evt)
	{
		Con::Warning("[sector-handoff] PostTo: create() returned null");
		return 0;
	}

	// Populate the typed wire fields with sample data so the receiver
	// side sees something meaningful instead of zero-fill from create().
	// Real handoff data lands in a later chunk once we have a hook on
	// the actual sector-transition trigger.
	{
		auto* e = reinterpret_cast<SectorHandoffEvent*>(evt);
		e->proto_version    = 2;
		e->flags            = 0x01;
		e->reserved0        = 0;
		e->seq              = 0x00000001u;
		e->handoff_token    = 0xCAFEF00DBADD1E55ull;
		for (int i = 0; i < 16; ++i) e->src_server_uuid[i] = std::uint8_t(0xA0 + i);
		for (int i = 0; i < 16; ++i) e->dst_server_uuid[i] = std::uint8_t(0xB0 + i);
		e->char_id          = 0x11112222u;
		e->account_id       = 0x33334444u;
		e->pos_x            = 1.5f;
		e->pos_y            = -2.5f;
		e->pos_z            = 100.25f;
		e->yaw              = 3.14159f;
		e->hp_q16           = 100u * 65536u;
		e->stamina_q16      = 50u  * 65536u;
		e->hunger_q16       = 30u  * 65536u;
		e->thirst_q16       = 20u  * 65536u;
		e->world_sector_id  = 451u;
		e->reserved_v2      = 0;
		e->world_x          = 12345.5f;
		e->world_y          = -6789.0f;
	}

	// Resolve the post vfn through conn + 0x1F8 (embedded GameConnection
	// subobject; its vtable starts there). vt[0] is NetConnection::postNetEvent
	// (we attributed this in PR #57). Call signature is
	// `(subObjThis, evt)` — extra args observed at the factory call site
	// are spurious.
	auto subObj = reinterpret_cast<std::uint8_t*>(conn) + 0x1F8;
	auto vt = *reinterpret_cast<void***>(subObj);
	using PostFn = std::uint8_t (__fastcall*)(void*, void*);
	auto post = reinterpret_cast<PostFn>(vt[0]);

	Con::Echo("[sector-handoff] PostTo: conn=%p subObj=%p vt=%p post=%p evt=%p",
	          conn, subObj, vt, vt[0], evt);

	std::uint8_t rc = post(subObj, evt);
	Con::Echo("[sector-handoff] PostTo: postNetEvent returned %u", (unsigned)rc);

	// Mirror the same 96 wire bytes onto the dispatcher daemon when a
	// target peer is configured. Off-engine path: best-effort, never
	// blocks the post path. Snapshot bytes BEFORE returning rc — the
	// engine may free `evt` after consuming.
	const auto& cfg = gServer.GetConfig();
	if (rc == 1 && !cfg.SectorHandoffTargetPeer.empty())
	{
		const auto* base = reinterpret_cast<const std::uint8_t*>(evt) + 0x48;
		std::vector<std::uint8_t> bytes(base, base + (sizeof(SectorHandoffEvent) - 0x48));
		const bool ok = Hooks::Dispatcher::SendTo(cfg.SectorHandoffTargetPeer, bytes);
		Con::Echo("[sector-handoff] forwarded %zu bytes to %s -> %s",
		          bytes.size(), cfg.SectorHandoffTargetPeer.c_str(),
		          ok ? "queued" : "DROPPED");
	}

	return rc;
}

void Hooks::SectorHandoff::DecodeAndLog(const std::string& from_peer_id,
                                        const std::vector<std::uint8_t>& payload)
{
	// v1 = 96 bytes, v2 = 104 bytes. Branch on proto_version after a
	// minimum-length check so we can decode either while we have v1
	// senders in the wild.
	constexpr std::size_t kWireSizeV1 = 96;
	constexpr std::size_t kWireSizeV2 = sizeof(SectorHandoffEvent) - 0x48; // 104
	static_assert(kWireSizeV2 == 104, "v2 wire size must be 104 bytes");

	if (payload.size() != kWireSizeV1 && payload.size() != kWireSizeV2)
	{
		Con::Warning("[sector-handoff] recv from=%s: wrong size %zu (want %zu or %zu) — ignoring",
		             from_peer_id.c_str(), payload.size(), kWireSizeV1, kWireSizeV2);
		return;
	}

	// Shared v1+v2 prefix (96 bytes). v2 adds 8 bytes at the end:
	// world_sector_id, reserved_v2, world_x, world_y.
	#pragma pack(push, 1)
	struct WireViewV1 {
		std::uint8_t  proto_version;       // +0x00
		std::uint8_t  flags;               // +0x01
		std::uint16_t reserved0;           // +0x02
		std::uint32_t seq;                 // +0x04
		std::uint64_t handoff_token;       // +0x08
		std::uint8_t  src_server_uuid[16]; // +0x10
		std::uint8_t  dst_server_uuid[16]; // +0x20
		std::uint32_t char_id;             // +0x30
		std::uint32_t account_id;          // +0x34
		float         pos_x, pos_y, pos_z, yaw;                    // +0x38..+0x47
		std::uint32_t hp_q16, stamina_q16, hunger_q16, thirst_q16; // +0x48..+0x57
	};
	struct WireViewV2Extra {
		std::uint32_t world_sector_id;     // +0x58
		std::uint32_t reserved_v2;         // +0x5C
		float         world_x;             // +0x60
		float         world_y;             // +0x64
	};
	#pragma pack(pop)
	static_assert(sizeof(WireViewV1)      == 88, "WireViewV1 must be 88 bytes");
	static_assert(sizeof(WireViewV2Extra) == 16, "WireViewV2Extra must be 16 bytes");

	WireViewV1 v;
	std::memcpy(&v, payload.data(), sizeof(v));

	if (v.proto_version == 1 && payload.size() == kWireSizeV1)
	{
		Con::Warning("[sector-handoff] recv from=%s: proto_version=1 is deprecated; "
		             "sender should upgrade to v2 (world_sector_id + world coords)",
		             from_peer_id.c_str());
	}
	else if (v.proto_version != 2)
	{
		Con::Warning("[sector-handoff] recv from=%s: unsupported proto_version=%u (size=%zu)",
		             from_peer_id.c_str(), (unsigned)v.proto_version, payload.size());
		return;
	}

	char src_hex[33], dst_hex[33];
	for (int i = 0; i < 16; ++i) std::snprintf(src_hex + i*2, 3, "%02x", v.src_server_uuid[i]);
	for (int i = 0; i < 16; ++i) std::snprintf(dst_hex + i*2, 3, "%02x", v.dst_server_uuid[i]);

	Con::Echo("[sector-handoff] recv from=%s proto=%u flags=0x%02X seq=%u token=0x%016llX",
	          from_peer_id.c_str(),
	          (unsigned)v.proto_version, (unsigned)v.flags, (unsigned)v.seq,
	          (unsigned long long)v.handoff_token);
	Con::Echo("[sector-handoff] recv src=%s dst=%s", src_hex, dst_hex);
	Con::Echo("[sector-handoff] recv char=%u acct=%u pos=(%.3f, %.3f, %.3f) yaw=%.5f",
	          (unsigned)v.char_id, (unsigned)v.account_id,
	          v.pos_x, v.pos_y, v.pos_z, v.yaw);
	Con::Echo("[sector-handoff] recv hp=%u sta=%u hun=%u thi=%u (Q16.16)",
	          (unsigned)v.hp_q16, (unsigned)v.stamina_q16,
	          (unsigned)v.hunger_q16, (unsigned)v.thirst_q16);

	if (payload.size() == kWireSizeV2)
	{
		WireViewV2Extra v2;
		std::memcpy(&v2, payload.data() + sizeof(WireViewV1), sizeof(v2));
		Con::Echo("[sector-handoff] recv world_sector=%u world_xy=(%.3f, %.3f)",
		          (unsigned)v2.world_sector_id, v2.world_x, v2.world_y);
	}
}

namespace
{
	// Build a v2 wire payload (104 bytes). Vitals / src+dst uuid /
	// pos_xyz / yaw stay placeholder values until chunk 13 plumbs real
	// engine state. char_id / world_sector_id / world_xy are caller-
	// supplied so this helper backs both the sample forward and the
	// scriptable triggerHandoff path.
	std::vector<std::uint8_t> buildV2Payload(std::uint32_t char_id,
	                                         std::uint32_t world_sector_id,
	                                         float world_x,
	                                         float world_y)
	{
		constexpr std::size_t kV2WireSize = sizeof(SectorHandoffEvent) - 0x48;
		static_assert(kV2WireSize == 104, "v2 wire size must be 104 bytes");
		std::vector<std::uint8_t> bytes(kV2WireSize, 0);
		auto put32 = [&](std::size_t off, std::uint32_t v) { std::memcpy(bytes.data() + off, &v, 4); };
		auto put64 = [&](std::size_t off, std::uint64_t v) { std::memcpy(bytes.data() + off, &v, 8); };
		auto putF  = [&](std::size_t off, float v)         { std::memcpy(bytes.data() + off, &v, 4); };

		bytes[0x00] = 2;                       // proto_version
		bytes[0x01] = 0x01;                    // flags
		put32(0x04, 0x00000001u);              // seq
		put64(0x08, 0xCAFEF00DBADD1E55ull);    // handoff_token
		for (int i = 0; i < 16; ++i) bytes[0x10 + i] = std::uint8_t(0xA0 + i); // src_uuid
		for (int i = 0; i < 16; ++i) bytes[0x20 + i] = std::uint8_t(0xB0 + i); // dst_uuid
		put32(0x30, char_id);
		put32(0x34, 0x33334444u);              // account_id placeholder
		putF (0x38, 1.5f);  putF(0x3C, -2.5f); putF(0x40, 100.25f); putF(0x44, 3.14159f);
		put32(0x48, 100u * 65536u);            // hp_q16
		put32(0x4C,  50u * 65536u);            // stamina_q16
		put32(0x50,  30u * 65536u);            // hunger_q16
		put32(0x54,  20u * 65536u);            // thirst_q16
		put32(0x58, world_sector_id);
		put32(0x5C, 0);                        // reserved_v2
		putF (0x60, world_x);
		putF (0x64, world_y);
		return bytes;
	}
}

void Hooks::SectorHandoff::TriggerSampleForward()
{
	const auto& cfg = gServer.GetConfig();
	if (cfg.SectorHandoffTargetPeer.empty())
	{
		Con::Echo("[sector-handoff] forward: no target peer configured, skipping");
		return;
	}

	auto bytes = buildV2Payload(0x11112222u, 451u, 12345.5f, -6789.0f);
	const bool ok = Hooks::Dispatcher::SendTo(cfg.SectorHandoffTargetPeer, bytes);
	Con::Echo("[sector-handoff] forwarded %zu bytes to %s -> %s",
	          bytes.size(), cfg.SectorHandoffTargetPeer.c_str(),
	          ok ? "queued" : "DROPPED");
}

bool Hooks::SectorHandoff::ForwardToSector(std::uint32_t char_id,
                                           std::uint32_t world_sector_id,
                                           float world_x,
                                           float world_y)
{
	return Hooks::Dispatcher::ResolveSector(world_sector_id,
		[char_id, world_sector_id, world_x, world_y](const Hooks::Dispatcher::ResolveResult& r)
		{
			if (!r.known)
			{
				Con::Warning("[sector-handoff] handoff: sector %u unknown — dropping (char=%u)",
				             (unsigned)world_sector_id, (unsigned)char_id);
				return;
			}
			auto bytes = buildV2Payload(char_id, world_sector_id, world_x, world_y);
			const bool ok = Hooks::Dispatcher::SendTo(r.peer_id, bytes);
			Con::Echo("[sector-handoff] handoff: char=%u sector=%u -> peer=%s : %zu bytes %s",
			          (unsigned)char_id, (unsigned)world_sector_id,
			          r.peer_id.c_str(), bytes.size(),
			          ok ? "queued" : "DROPPED");
		});
}

void Hooks::SectorHandoff::InjectFromWire(const std::string& from_peer_id,
                                          const std::vector<std::uint8_t>& payload)
{
	constexpr std::size_t kWireSize = sizeof(SectorHandoffEvent) - 0x48; // 96
	if (payload.size() != kWireSize)
	{
		Con::Warning("[sector-handoff] inject from=%s: wrong size %zu (want %zu) — ignoring",
		             from_peer_id.c_str(), payload.size(), kWireSize);
		return;
	}
	if (!g_registered.load(std::memory_order_acquire))
	{
		Con::Warning("[sector-handoff] inject from=%s: rep not yet registered", from_peer_id.c_str());
		return;
	}

	// Allocate a fresh event via our slot-7 create(). That path zeroes
	// the struct and stamps our event vftable at offset 0, so the
	// engine-side base bytes (+0x00..+0x47) are already sane.
	using CreateFn = void* (__fastcall*)(void*, void*, void*, void*);
	auto createSlot = reinterpret_cast<CreateFn>(g_classRepVtable[7]);
	void* evt = createSlot(g_classRep, nullptr, nullptr, nullptr);
	if (!evt)
	{
		Con::Warning("[sector-handoff] inject from=%s: create() returned null", from_peer_id.c_str());
		return;
	}

	// Direct memcpy of the wire bytes into the payload region. This
	// bypasses BitStream::readInt — we control both sides of the
	// dispatcher payload, so the 24-u32 layout is guaranteed identical
	// to what ourPack emitted. When the engine actually delivers a
	// SectorHandoff via the real receive path (FUN_140541720), it'll
	// call ourUnpack via slot 8 — that path is chunk 9.
	auto* dst = reinterpret_cast<std::uint8_t*>(evt) + 0x48;
	std::memcpy(dst, payload.data(), kWireSize);

	// Read back through the typed view to confirm the populated event
	// looks the same as DecodeAndLog reports — proves the engine-side
	// memory has the correct layout and the vftable stamp survived.
	auto* e = reinterpret_cast<const SectorHandoffEvent*>(evt);
	Con::Echo("[sector-handoff] inject from=%s: evt=%p vftable=%p proto=%u seq=%u char=%u pos=(%.3f, %.3f, %.3f)",
	          from_peer_id.c_str(), evt, *reinterpret_cast<void**>(evt),
	          (unsigned)e->proto_version, (unsigned)e->seq, (unsigned)e->char_id,
	          e->pos_x, e->pos_y, e->pos_z);

	// Drive engine-style process via the event vftable — same call shape
	// the engine itself would make after a real wire receive (slot 13
	// per issue #87 RE). For a synthesized event there's no real
	// NetConnection*, so pass nullptr; ourProcess tolerates it.
	auto** vt = *reinterpret_cast<void***>(evt);
	using ProcessFn = std::uint8_t (__fastcall*)(void*, void*);
	auto process = reinterpret_cast<ProcessFn>(vt[kEventVftableSlot_Process]);
	const std::uint8_t rc = process(evt, nullptr);
	Con::Echo("[sector-handoff] inject from=%s: process returned %u",
	          from_peer_id.c_str(), (unsigned)rc);

	::operator delete(evt);
}

